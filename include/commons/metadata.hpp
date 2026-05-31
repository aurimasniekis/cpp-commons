#pragma once

/// @file
/// @brief `comms::md` — a JSON-like dynamic value tree.
///
/// A single self-contained header providing `comms::md::Value` (a discriminated
/// union of null/bool/int/uint/float/double/string/`Array`/`Object`), the
/// `Array` and `Object` containers, dotted-path lookup, deep merge, hashing,
/// compact-JSON `operator<<` / `std::format`, and free-function helpers. The
/// headline document-root case is also surfaced at the Commons root as
/// `comms::Metadata` (an alias for `comms::md::Object`).
///
/// Per the no-forced-dependency rule this header carries no nlohmann/json — the
/// `Value`/`Object` JSON round-trip lives in `commons/json.hpp` (specifically
/// `commons/json/metadata.hpp`) under `COMMONS_WITH_NLOHMANN_JSON`.

#include <commons/exception.hpp>

#include <array>
#include <cctype>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <initializer_list>
#include <memory>
#include <ostream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace comms::md {

class Value;
class Object;

/// Sequence container alias used for the array alternative of `Value`.
using Array = std::vector<Value>;

namespace detail {

// Transparent hash + equal-to lets std::unordered_map<std::string, ...> accept
// std::string_view / const char* in find/contains/erase without allocating.
struct TransparentStringHash {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(const std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
    [[nodiscard]] std::size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
    [[nodiscard]] std::size_t operator()(const char* s) const noexcept {
        return std::hash<std::string_view>{}(std::string_view{s});
    }
};

// Concepts that exclude bool and character types so that Value{true} stays
// a bool and Value{'a'} doesn't sneak in as an integer.
template <class T>
concept SignedIntLike =
    std::is_integral_v<T> && std::is_signed_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool> &&
    !std::is_same_v<std::remove_cv_t<T>, char> && !std::is_same_v<std::remove_cv_t<T>, wchar_t> &&
    !std::is_same_v<std::remove_cv_t<T>, char8_t> &&
    !std::is_same_v<std::remove_cv_t<T>, char16_t> &&
    !std::is_same_v<std::remove_cv_t<T>, char32_t>;

template <class T>
concept UnsignedIntLike =
    std::is_integral_v<T> && std::is_unsigned_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool> &&
    !std::is_same_v<std::remove_cv_t<T>, char> && !std::is_same_v<std::remove_cv_t<T>, wchar_t> &&
    !std::is_same_v<std::remove_cv_t<T>, char8_t> &&
    !std::is_same_v<std::remove_cv_t<T>, char16_t> &&
    !std::is_same_v<std::remove_cv_t<T>, char32_t>;

template <class T>
concept FloatLike = std::is_floating_point_v<T>;

}  // namespace detail

// =====================================================================
//   Exceptions — rooted at comms::Exception so the whole Commons family
//   is catchable with a single `catch (const comms::Exception&)`.
// =====================================================================

/// Base class for all exceptions thrown by the `comms::md` types.
class MetadataError : public comms::Exception {
public:
    using comms::Exception::Exception;
};

/// Thrown when a required key or path is not present in an `Object`.
class MissingKeyError : public MetadataError {
public:
    using MetadataError::MetadataError;
};

/// Thrown when a `Value` holds a different alternative than the caller demanded.
class TypeError : public MetadataError {
public:
    using MetadataError::MetadataError;
};

/// Discriminated union holding one of the JSON-like alternatives
/// (null, bool, signed/unsigned integer, float/double, string, array, or
/// nested object).
class Value {
public:
    // Object is indirected through unique_ptr so Value can recursively hold an
    // Object that itself stores Values. Without indirection, std::variant
    // probes traits like is_*_constructible<Object> at instantiation time,
    // which would require Object complete here.
    /// Underlying `std::variant` type that stores the active alternative.
    using variant_type = std::variant<std::nullptr_t,
                                      bool,
                                      std::int64_t,
                                      std::uint64_t,
                                      float,
                                      double,
                                      std::string,
                                      Array,
                                      std::unique_ptr<Object>>;

    // Every member that touches v_ is DECLARED here and DEFINED out-of-line
    // below. Reason: instantiating variant<..., unique_ptr<Object>>
    // operations (including the constructor's exception-handling rollback)
    // requires unique_ptr<Object>::~unique_ptr to be instantiable, which
    // requires Object complete — which isn't the case until Object is defined.

    /// Construct a null-valued `Value`.
    Value() noexcept;
    /// Move-construct from another `Value`, leaving the source in a valid but unspecified state.
    Value(Value&& other) noexcept;
    /// Move-assign from another `Value`.
    Value& operator=(Value&& other) noexcept;
    /// Deep-copy construct from another `Value`.
    Value(const Value& other);
    /// Deep-copy assign from another `Value`.
    Value& operator=(const Value& other);
    /// Destructor.
    ~Value();

    // Braced-list assignment for ergonomic in-place writes:
    //   m["tags"] = {"a", "b"};          // → Array
    //   m["sub"]  = {{"k", 1}, {"k2", 2}}; // → Object (pair-like elements)
    // The two overloads disambiguate by element type: bare values pick the
    // Array overload; brace-pair elements pick the Object overload (Value
    // has no 2-arg constructor, so they only match the pair form).
    // An empty list `= {}` is ambiguous — use `comms::md::Array{}` or
    // `comms::md::Object{}` explicitly when you mean empty.
    /// Assign an `Array` from a braced list of values.
    Value& operator=(std::initializer_list<Value> il);
    /// Assign an `Object` from a braced list of key/value pairs.
    Value& operator=(std::initializer_list<std::pair<const std::string, Value>> il);

    /// Construct a null-valued `Value`.
    Value(std::nullptr_t) noexcept;

    /// Construct a boolean-valued `Value`.
    template <class B>
        requires std::same_as<B, bool>
    Value(B b) noexcept;

    /// Construct a signed-integer `Value` (stored as `std::int64_t`).
    template <detail::SignedIntLike T>
    Value(T x) noexcept;

    /// Construct an unsigned-integer `Value` (stored as `std::uint64_t`).
    template <detail::UnsignedIntLike T>
    Value(T x) noexcept;

    // FloatLike covers float/double/long double; the constructor stores
    // single-precision input as `float` and everything else as `double`.
    /// Construct a floating-point `Value`; `float` stays `float`, others widen to `double`.
    template <detail::FloatLike T>
    Value(T x) noexcept;

    /// Construct a string-valued `Value` from an owning `std::string`.
    Value(std::string s);
    /// Construct a string-valued `Value` from a `std::string_view`.
    Value(std::string_view s);
    /// Construct a string-valued `Value` from a C string.
    Value(const char* s);

    /// Construct an array-valued `Value`.
    Value(Array a);
    /// Construct an object-valued `Value`.
    Value(Object o);

    // Build an Object directly from a pair-shaped braced list. Lets nested
    // objects parse naturally:
    //   m["sub"] = {{"a", {{"b", 1}}}};  // outer Object, inner Object
    // We deliberately do NOT add `Value(initializer_list<Value>)` for arrays
    // because that would change the meaning of `Value{42}` from scalar to
    // single-element Array.
    /// Construct an object-valued `Value` from a braced list of key/value pairs.
    Value(std::initializer_list<std::pair<const std::string, Value>> il);

    /// Deleted to prevent silent pointer-to-bool conversions.
    template <class T>
    Value(T*) = delete;

    // --- predicates --------------------------------------------------

    /// True if the value holds `nullptr`.
    [[nodiscard]] bool is_null() const noexcept;
    /// True if the value holds a `bool`.
    [[nodiscard]] bool is_bool() const noexcept;
    /// True if the value holds a signed integer (`std::int64_t`).
    [[nodiscard]] bool is_int() const noexcept;
    /// True if the value holds an unsigned integer (`std::uint64_t`).
    [[nodiscard]] bool is_uint() const noexcept;
    /// True if the value holds a `float`.
    [[nodiscard]] bool is_float() const noexcept;
    /// True if the value holds a `double`.
    [[nodiscard]] bool is_double() const noexcept;
    /// True if the value holds any numeric alternative.
    [[nodiscard]] bool is_number() const noexcept;
    /// True if the value holds a `std::string`.
    [[nodiscard]] bool is_string() const noexcept;
    /// True if the value holds an `Array`.
    [[nodiscard]] bool is_array() const noexcept;
    /// True if the value holds a nested `Object`.
    [[nodiscard]] bool is_object() const noexcept;

    // --- strict accessors -------------------------------------------

    /// Return the bool; throws `std::bad_variant_access` on type mismatch.
    [[nodiscard]] bool as_bool() const;
    /// Return the signed integer; throws on type mismatch.
    [[nodiscard]] std::int64_t as_int() const;
    /// Return the unsigned integer; throws on type mismatch.
    [[nodiscard]] std::uint64_t as_uint() const;
    /// Return the `float`; strict — throws if the value isn't a `float`.
    [[nodiscard]] float as_float() const;
    /// Return as `double`, widening from `int64`/`uint64`/`float` as needed.
    [[nodiscard]] double as_double() const;

    /// Access the string alternative; throws on type mismatch.
    [[nodiscard]] std::string& as_string();
    /// Access the string alternative; throws on type mismatch.
    [[nodiscard]] const std::string& as_string() const;
    /// Access the array alternative; throws on type mismatch.
    [[nodiscard]] Array& as_array();
    /// Access the array alternative; throws on type mismatch.
    [[nodiscard]] const Array& as_array() const;
    /// Access the nested object; throws on type mismatch.
    [[nodiscard]] Object& as_object();
    /// Access the nested object; throws on type mismatch.
    [[nodiscard]] const Object& as_object() const;

    // --- pointer accessors (noexcept) -------------------------------

    /// Return a pointer to the bool, or `nullptr` if the value isn't a bool.
    [[nodiscard]] bool* as_bool_if() noexcept;
    /// Return a pointer to the bool, or `nullptr` if the value isn't a bool.
    [[nodiscard]] const bool* as_bool_if() const noexcept;
    /// Return a pointer to the signed integer, or `nullptr` if not held.
    [[nodiscard]] std::int64_t* as_int_if() noexcept;
    /// Return a pointer to the signed integer, or `nullptr` if not held.
    [[nodiscard]] const std::int64_t* as_int_if() const noexcept;
    /// Return a pointer to the unsigned integer, or `nullptr` if not held.
    [[nodiscard]] std::uint64_t* as_uint_if() noexcept;
    /// Return a pointer to the unsigned integer, or `nullptr` if not held.
    [[nodiscard]] const std::uint64_t* as_uint_if() const noexcept;
    /// Return a pointer to the `float`, or `nullptr` if not held.
    [[nodiscard]] float* as_float_if() noexcept;
    /// Return a pointer to the `float`, or `nullptr` if not held.
    [[nodiscard]] const float* as_float_if() const noexcept;
    /// Return a pointer to the `double`, or `nullptr` if not held.
    [[nodiscard]] double* as_double_if() noexcept;
    /// Return a pointer to the `double`, or `nullptr` if not held.
    [[nodiscard]] const double* as_double_if() const noexcept;
    /// Return a pointer to the string, or `nullptr` if not held.
    [[nodiscard]] std::string* as_string_if() noexcept;
    /// Return a pointer to the string, or `nullptr` if not held.
    [[nodiscard]] const std::string* as_string_if() const noexcept;
    /// Return a pointer to the array, or `nullptr` if not held.
    [[nodiscard]] Array* as_array_if() noexcept;
    /// Return a pointer to the array, or `nullptr` if not held.
    [[nodiscard]] const Array* as_array_if() const noexcept;
    /// Return a pointer to the nested object, or `nullptr` if not held.
    [[nodiscard]] Object* as_object_if() noexcept;
    /// Return a pointer to the nested object, or `nullptr` if not held.
    [[nodiscard]] const Object* as_object_if() const noexcept;

    /// Return a pointer to the alternative of type `T`, or `nullptr` if not held.
    template <class T>
    [[nodiscard]] T* get_if() noexcept;
    /// Return a pointer to the alternative of type `T`, or `nullptr` if not held.
    template <class T>
    [[nodiscard]] const T* get_if() const noexcept;

    /// Return the held `T` by value, or `fallback` if a different alternative is active.
    template <class T>
    [[nodiscard]] T value_or(T fallback) const;

    /// Return the underlying `std::variant` for advanced access.
    [[nodiscard]] variant_type& raw() noexcept;
    /// Return the underlying `std::variant` for advanced access.
    [[nodiscard]] const variant_type& raw() const noexcept;

    /// Return the zero-based index of the active alternative.
    [[nodiscard]] std::size_t index() const noexcept;

    /// Deep value-equality compare two `Value`s.
    friend bool operator==(const Value& a, const Value& b) noexcept;
    /// Negation of `operator==`.
    friend bool operator!=(const Value& a, const Value& b) noexcept;

private:
    variant_type v_;
};

// --- factory helper declarations ----------------------------------------

/// Construct a null-valued `Value`.
[[nodiscard]] Value null() noexcept;
/// Construct a boolean-valued `Value`.
[[nodiscard]] Value boolean(bool b) noexcept;

/// Construct a numeric `Value` from any signed, unsigned, or floating type.
template <class T>
    requires(detail::SignedIntLike<T> || detail::UnsignedIntLike<T> || detail::FloatLike<T>)
[[nodiscard]] inline Value number(T v) noexcept {
    return Value{v};
}

/// Construct a string `Value` from an owning `std::string`.
[[nodiscard]] Value string(std::string s);
/// Construct a string `Value` from a `std::string_view`.
[[nodiscard]] Value string(std::string_view s);
/// Construct a string `Value` from a C string.
[[nodiscard]] Value string(const char* s);

/// Construct an empty `Array`.
[[nodiscard]] inline Array array() {
    return Array{};
}
/// Construct an `Array` from a braced list of values.
[[nodiscard]] inline Array array(const std::initializer_list<Value> il) {
    return Array(il);
}

// object() / object({...}) factories are defined after Object below.

/// Ordered-by-insertion-time-ish string-keyed map of `Value`s, with
/// transparent string-view lookup and JSON-flavored metadata helpers.
class Object {
public:
    /// Underlying associative container type.
    using map_type =
        std::unordered_map<std::string, Value, detail::TransparentStringHash, std::equal_to<>>;

    /// Key type (always `std::string`).
    using key_type = map_type::key_type;
    /// Mapped value type (always `Value`).
    using mapped_type = map_type::mapped_type;
    /// `std::pair<const key_type, mapped_type>`.
    using value_type = map_type::value_type;
    /// Unsigned size type.
    using size_type = map_type::size_type;
    /// Signed difference type for iterators.
    using difference_type = map_type::difference_type;
    /// Hash functor type.
    using hasher = map_type::hasher;
    /// Key-equality functor type.
    using key_equal = map_type::key_equal;
    /// Reference to a stored `value_type`.
    using reference = map_type::reference;
    /// Const reference to a stored `value_type`.
    using const_reference = map_type::const_reference;
    /// Pointer to a stored `value_type`.
    using pointer = map_type::pointer;
    /// Const pointer to a stored `value_type`.
    using const_pointer = map_type::const_pointer;
    /// Mutable iterator type.
    using iterator = map_type::iterator;
    /// Const iterator type.
    using const_iterator = map_type::const_iterator;

    /// Construct an empty `Object`.
    Object() = default;

    /// Construct from a braced list of key/value pairs.
    Object(const std::initializer_list<value_type> il) : map_(il) {}

    /// Construct from an iterator range of key/value pairs.
    template <class InputIt>
    Object(InputIt first, InputIt last) : map_(first, last) {}

    // --- map-like surface ---------------------------------------------

    /// Iterator to the first element.
    iterator begin() noexcept {
        return map_.begin();
    }
    /// Const iterator to the first element.
    const_iterator begin() const noexcept {
        return map_.begin();
    }
    /// Const iterator to the first element.
    const_iterator cbegin() const noexcept {
        return map_.cbegin();
    }
    /// Iterator past the last element.
    iterator end() noexcept {
        return map_.end();
    }
    /// Const iterator past the last element.
    const_iterator end() const noexcept {
        return map_.end();
    }
    /// Const iterator past the last element.
    const_iterator cend() const noexcept {
        return map_.cend();
    }

    /// True if the object has no entries.
    [[nodiscard]] bool empty() const noexcept {
        return map_.empty();
    }
    /// Number of entries.
    [[nodiscard]] size_type size() const noexcept {
        return map_.size();
    }

    /// Remove all entries.
    void clear() noexcept {
        map_.clear();
    }
    /// Reserve storage for at least `n` entries.
    void reserve(const size_type n) {
        map_.reserve(n);
    }

    /// Access (and default-insert if missing) the value for `key`.
    Value& operator[](const std::string_view key) {
        if (const auto it = map_.find(key); it != map_.end()) {
            return it->second;
        }
        return map_.emplace(std::string(key), Value{}).first->second;
    }
    /// Access (and default-insert if missing) the value for `key`.
    Value& operator[](const std::string& key) {
        return map_[key];
    }
    /// Access (and default-insert if missing) the value for `key`.
    Value& operator[](std::string&& key) {
        return map_[std::move(key)];
    }
    /// Access (and default-insert if missing) the value for `key`.
    Value& operator[](const char* key) {
        return (*this)[std::string_view{key}];
    }

    /// Access the value for `key`; throws `std::out_of_range` if missing.
    Value& at(const std::string_view key) {
        if (const auto it = map_.find(key); it != map_.end()) {
            return it->second;
        }
        throw std::out_of_range("Object::at: key not found");
    }
    /// Access the value for `key`; throws `std::out_of_range` if missing.
    const Value& at(const std::string_view key) const {
        if (const auto it = map_.find(key); it != map_.end()) {
            return it->second;
        }
        throw std::out_of_range("Object::at: key not found");
    }

    /// Find an entry by key, returning `end()` on miss.
    iterator find(const std::string_view key) {
        return map_.find(key);
    }
    /// Find an entry by key, returning `end()` on miss.
    const_iterator find(const std::string_view key) const {
        return map_.find(key);
    }

    /// Return `1` if `key` is present, otherwise `0`.
    [[nodiscard]] size_type count(const std::string_view key) const {
        return map_.count(key);
    }

    /// Insert or overwrite the entry for `k`.
    template <class K, class V>
    std::pair<iterator, bool> insert_or_assign(K&& k, V&& v) {
        return map_.insert_or_assign(std::forward<K>(k), std::forward<V>(v));
    }

    /// Construct an entry in place; no effect if `k` already exists.
    template <class K, class V>
    std::pair<iterator, bool> emplace(K&& k, V&& v) {
        return map_.emplace(std::forward<K>(k), std::forward<V>(v));
    }

    /// Insert an entry; no effect if the key already exists.
    std::pair<iterator, bool> insert(const value_type& v) {
        return map_.insert(v);
    }
    /// Insert an entry; no effect if the key already exists.
    std::pair<iterator, bool> insert(value_type&& v) {
        return map_.insert(std::move(v));
    }

    /// Erase the entry for `key`; returns `1` if removed, otherwise `0`.
    size_type erase(const std::string_view key) {
        if (const auto it = map_.find(key); it != map_.end()) {
            map_.erase(it);
            return 1;
        }
        return 0;
    }
    /// Erase the entry at `pos`; returns the next iterator.
    iterator erase(const const_iterator pos) {
        return map_.erase(pos);
    }

    // --- metadata helpers (methods) -----------------------------------

    /// True if `key` is present in the object.
    [[nodiscard]] bool contains(const std::string_view key) const {
        return map_.contains(key);
    }

    /// Return a pointer to the value for `key`, or `nullptr` if absent.
    [[nodiscard]] Value* find_ptr(const std::string_view key) {
        if (const auto it = map_.find(key); it != map_.end()) {
            return &it->second;
        }
        return nullptr;
    }
    /// Return a pointer to the value for `key`, or `nullptr` if absent.
    [[nodiscard]] const Value* find_ptr(const std::string_view key) const {
        if (const auto it = map_.find(key); it != map_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    /// Return the value for `key`; throws `MissingKeyError` if absent.
    Value& require(const std::string_view key) {
        if (auto* p = find_ptr(key); p != nullptr) {
            return *p;
        }
        throw MissingKeyError("Object::require: missing key '" + std::string(key) + "'");
    }
    /// Return the value for `key`; throws `MissingKeyError` if absent.
    const Value& require(const std::string_view key) const {
        if (const auto* p = find_ptr(key); p != nullptr) {
            return *p;
        }
        throw MissingKeyError("Object::require: missing key '" + std::string(key) + "'");
    }

    /// Return the string at `key`; throws on missing key or type mismatch.
    std::string& require_string(const std::string_view key) {
        Value& v = require(key);
        if (auto* p = v.as_string_if(); p != nullptr) {
            return *p;
        }
        throw TypeError("Object::require_string: '" + std::string(key) + "' is not a string");
    }
    /// Return the string at `key`; throws on missing key or type mismatch.
    const std::string& require_string(const std::string_view key) const {
        const Value& v = require(key);
        if (const auto* p = v.as_string_if(); p != nullptr) {
            return *p;
        }
        throw TypeError("Object::require_string: '" + std::string(key) + "' is not a string");
    }

    /// Return the array at `key`; throws on missing key or type mismatch.
    Array& require_array(const std::string_view key) {
        Value& v = require(key);
        if (auto* p = v.as_array_if(); p != nullptr) {
            return *p;
        }
        throw TypeError("Object::require_array: '" + std::string(key) + "' is not an array");
    }
    /// Return the array at `key`; throws on missing key or type mismatch.
    const Array& require_array(const std::string_view key) const {
        const Value& v = require(key);
        if (const auto* p = v.as_array_if(); p != nullptr) {
            return *p;
        }
        throw TypeError("Object::require_array: '" + std::string(key) + "' is not an array");
    }

    /// Return the nested object at `key`; throws on missing key or type mismatch.
    Object& require_object(const std::string_view key) {
        Value& v = require(key);
        if (auto* p = v.as_object_if(); p != nullptr) {
            return *p;
        }
        throw TypeError("Object::require_object: '" + std::string(key) + "' is not an object");
    }
    /// Return the nested object at `key`; throws on missing key or type mismatch.
    const Object& require_object(const std::string_view key) const {
        const Value& v = require(key);
        if (const auto* p = v.as_object_if(); p != nullptr) {
            return *p;
        }
        throw TypeError("Object::require_object: '" + std::string(key) + "' is not an object");
    }

    /// Pointer to the string at `key`, or `nullptr` if absent or wrong type.
    [[nodiscard]] const std::string* get_string_if(const std::string_view key) const {
        if (const auto* v = find_ptr(key); v != nullptr) {
            return v->as_string_if();
        }
        return nullptr;
    }
    /// Pointer to the array at `key`, or `nullptr` if absent or wrong type.
    [[nodiscard]] const Array* get_array_if(const std::string_view key) const {
        if (const auto* v = find_ptr(key); v != nullptr) {
            return v->as_array_if();
        }
        return nullptr;
    }
    /// Pointer to the nested object at `key`, or `nullptr` if absent or wrong type.
    [[nodiscard]] const Object* get_object_if(const std::string_view key) const {
        if (const auto* v = find_ptr(key); v != nullptr) {
            return v->as_object_if();
        }
        return nullptr;
    }

    // --- path helpers (definitions live below) ------------------------

    /// Find a value by dotted path (e.g. `"a.b[0].c"`), or `nullptr` on miss.
    [[nodiscard]] Value* find_path(std::string_view path);
    /// Find a value by dotted path (e.g. `"a.b[0].c"`), or `nullptr` on miss.
    [[nodiscard]] const Value* find_path(std::string_view path) const;
    /// Return the value at `path`; throws on miss or malformed path.
    [[nodiscard]] const Value& require_path(std::string_view path) const;
    /// Return the value at `path`; throws on miss or malformed path.
    [[nodiscard]] Value& require_path(std::string_view path);
    /// True if `path` resolves to a value in the object.
    [[nodiscard]] bool contains_path(std::string_view path) const;

    // --- deep merge: source wins on non-object conflicts; arrays replaced ---

    /// Deep-merge `source` into this object; nested objects recurse, other
    /// alternatives are overwritten, and arrays are replaced wholesale.
    void merge(const Object& source);

    /// Access the underlying `std::unordered_map`.
    [[nodiscard]] map_type& raw() noexcept {
        return map_;
    }
    /// Access the underlying `std::unordered_map`.
    [[nodiscard]] const map_type& raw() const noexcept {
        return map_;
    }

    /// Deep value-equality compare two `Object`s.
    friend bool operator==(const Object& a, const Object& b) noexcept {
        return a.map_ == b.map_;
    }
    /// Negation of `operator==`.
    friend bool operator!=(const Object& a, const Object& b) noexcept {
        return !(a == b);
    }

private:
    map_type map_;
};

/// Convenience alias — `Metadata` is the canonical name for a top-level `Object`.
using Metadata = Object;

// =====================================================================
//   Value out-of-line definitions — defined here so that Object is
//   complete by the time we touch ~unique_ptr<Object>, *Object, etc.
// =====================================================================

inline Value::Value() noexcept : v_(nullptr) {}
inline Value::Value(std::nullptr_t) noexcept : v_(nullptr) {}

template <class B>
    requires std::same_as<B, bool>
inline Value::Value(B b) noexcept : v_(b) {}

template <detail::SignedIntLike T>
inline Value::Value(T x) noexcept : v_(static_cast<std::int64_t>(x)) {}

template <detail::UnsignedIntLike T>
inline Value::Value(T x) noexcept : v_(static_cast<std::uint64_t>(x)) {}

template <detail::FloatLike T>
inline Value::Value(T x) noexcept {
    if constexpr (std::is_same_v<std::remove_cv_t<T>, float>) {
        v_ = x;
    } else {
        v_ = static_cast<double>(x);
    }
}

inline Value::Value(std::string s) : v_(std::move(s)) {}
inline Value::Value(const std::string_view s) : v_(std::string(s)) {}
inline Value::Value(const char* s) : v_(std::string(s)) {}

inline Value::Value(Array a) : v_(std::move(a)) {}

inline Value::Value(Object o) : v_(std::make_unique<Object>(std::move(o))) {}

inline Value::Value(const std::initializer_list<std::pair<const std::string, Value>> il)
    : v_(std::make_unique<Object>(Object(il))) {}

inline Value::Value(Value&& other) noexcept : v_(std::move(other.v_)) {}

inline Value& Value::operator=(Value&& other) noexcept {
    v_ = std::move(other.v_);
    return *this;
}

inline Value::Value(const Value& other) {
    std::visit(
        [&](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<Object>>) {
                v_ = std::make_unique<Object>(*x);
            } else {
                v_ = x;
            }
        },
        other.v_);
}

inline Value& Value::operator=(const Value& other) {
    if (this == &other) {
        return *this;
    }
    Value tmp(other);
    v_ = std::move(tmp.v_);
    return *this;
}

inline Value& Value::operator=(const std::initializer_list<Value> il) {
    v_ = Array(il);
    return *this;
}

inline Value&
Value::operator=(const std::initializer_list<std::pair<const std::string, Value>> il) {
    v_ = std::make_unique<Object>(Object(il));
    return *this;
}

// NOLINTNEXTLINE(readability-redundant-inline-specifier)
inline Value::~Value() = default;

inline bool Value::is_null() const noexcept {
    return std::holds_alternative<std::nullptr_t>(v_);
}
inline bool Value::is_bool() const noexcept {
    return std::holds_alternative<bool>(v_);
}
inline bool Value::is_int() const noexcept {
    return std::holds_alternative<std::int64_t>(v_);
}
inline bool Value::is_uint() const noexcept {
    return std::holds_alternative<std::uint64_t>(v_);
}
inline bool Value::is_float() const noexcept {
    return std::holds_alternative<float>(v_);
}
inline bool Value::is_double() const noexcept {
    return std::holds_alternative<double>(v_);
}
inline bool Value::is_number() const noexcept {
    return is_int() || is_uint() || is_float() || is_double();
}
inline bool Value::is_string() const noexcept {
    return std::holds_alternative<std::string>(v_);
}
inline bool Value::is_array() const noexcept {
    return std::holds_alternative<Array>(v_);
}
inline bool Value::is_object() const noexcept {
    return std::holds_alternative<std::unique_ptr<Object>>(v_);
}

inline bool Value::as_bool() const {
    return std::get<bool>(v_);
}
inline std::int64_t Value::as_int() const {
    return std::get<std::int64_t>(v_);
}
inline std::uint64_t Value::as_uint() const {
    return std::get<std::uint64_t>(v_);
}

inline float Value::as_float() const {
    return std::get<float>(v_);
}

inline double Value::as_double() const {
    if (const auto* p = std::get_if<double>(&v_); p != nullptr) {
        return *p;
    }
    if (const auto* p = std::get_if<float>(&v_); p != nullptr) {
        return static_cast<double>(*p);
    }
    if (const auto* p = std::get_if<std::int64_t>(&v_); p != nullptr) {
        return static_cast<double>(*p);
    }
    if (const auto* p = std::get_if<std::uint64_t>(&v_); p != nullptr) {
        return static_cast<double>(*p);
    }
    throw TypeError("Value::as_double: value is not a number");
}

inline std::string& Value::as_string() {
    return std::get<std::string>(v_);
}
inline const std::string& Value::as_string() const {
    return std::get<std::string>(v_);
}
inline Array& Value::as_array() {
    return std::get<Array>(v_);
}
inline const Array& Value::as_array() const {
    return std::get<Array>(v_);
}

inline Object& Value::as_object() {
    return *std::get<std::unique_ptr<Object>>(v_);
}
inline const Object& Value::as_object() const {
    return *std::get<std::unique_ptr<Object>>(v_);
}

inline bool* Value::as_bool_if() noexcept {
    return std::get_if<bool>(&v_);
}
inline const bool* Value::as_bool_if() const noexcept {
    return std::get_if<bool>(&v_);
}
inline std::int64_t* Value::as_int_if() noexcept {
    return std::get_if<std::int64_t>(&v_);
}
inline const std::int64_t* Value::as_int_if() const noexcept {
    return std::get_if<std::int64_t>(&v_);
}
inline std::uint64_t* Value::as_uint_if() noexcept {
    return std::get_if<std::uint64_t>(&v_);
}
inline const std::uint64_t* Value::as_uint_if() const noexcept {
    return std::get_if<std::uint64_t>(&v_);
}
inline float* Value::as_float_if() noexcept {
    return std::get_if<float>(&v_);
}
inline const float* Value::as_float_if() const noexcept {
    return std::get_if<float>(&v_);
}
inline double* Value::as_double_if() noexcept {
    return std::get_if<double>(&v_);
}
inline const double* Value::as_double_if() const noexcept {
    return std::get_if<double>(&v_);
}

inline std::string* Value::as_string_if() noexcept {
    return std::get_if<std::string>(&v_);
}
inline const std::string* Value::as_string_if() const noexcept {
    return std::get_if<std::string>(&v_);
}
inline Array* Value::as_array_if() noexcept {
    return std::get_if<Array>(&v_);
}
inline const Array* Value::as_array_if() const noexcept {
    return std::get_if<Array>(&v_);
}

inline Object* Value::as_object_if() noexcept {
    if (const auto* p = std::get_if<std::unique_ptr<Object>>(&v_); p != nullptr) {
        return p->get();
    }
    return nullptr;
}
inline const Object* Value::as_object_if() const noexcept {
    if (const auto* p = std::get_if<std::unique_ptr<Object>>(&v_); p != nullptr) {
        return p->get();
    }
    return nullptr;
}

inline bool operator==(const Value& a, const Value& b) noexcept {
    if (a.v_.index() != b.v_.index()) {
        return false;
    }
    return std::visit(
        [&](const auto& x) -> bool {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<Object>>) {
                const auto& y = std::get<std::unique_ptr<Object>>(b.v_);
                return *x == *y;
            } else {
                const auto& y = std::get<T>(b.v_);
                return x == y;
            }
        },
        a.v_);
}

inline bool operator!=(const Value& a, const Value& b) noexcept {
    return !(a == b);
}

template <class T>
inline T* Value::get_if() noexcept {
    return std::get_if<T>(&v_);
}
template <class T>
inline const T* Value::get_if() const noexcept {
    return std::get_if<T>(&v_);
}

template <class T>
inline T Value::value_or(T fallback) const {
    if (auto* p = std::get_if<T>(&v_)) {
        return *p;
    }
    return fallback;
}

inline Value::variant_type& Value::raw() noexcept {
    return v_;
}
inline const Value::variant_type& Value::raw() const noexcept {
    return v_;
}

inline std::size_t Value::index() const noexcept {
    return v_.index();
}

// --- Value factory helpers (out-of-line because string ones touch std::string) ---

inline Value null() noexcept {
    return Value{};
}
inline Value boolean(const bool b) noexcept {
    return Value{b};  // routes through the constrained <same_as<bool>> template
}
inline Value string(std::string s) {
    return Value{std::move(s)};
}
inline Value string(const std::string_view s) {
    return Value{s};
}
inline Value string(const char* s) {
    return Value{s};
}

// --- object() factory helpers ---

/// Construct an empty `Object`.
[[nodiscard]] inline Object object() {
    return Object{};
}
/// Construct an `Object` from a braced list of key/value pairs.
[[nodiscard]] inline Object object(const std::initializer_list<Object::value_type> il) {
    return Object(il);
}

// --- Object::merge (defined after Value is complete) ---

inline void Object::merge(const Object& source) {
    for (const auto& [k, v] : source.map_) {
        auto it = map_.find(k);
        if (it == map_.end()) {
            map_.emplace(k, v);
            continue;
        }
        Object* dst_obj = it->second.as_object_if();
        if (const Object* src_obj = v.as_object_if(); dst_obj != nullptr && src_obj != nullptr) {
            dst_obj->merge(*src_obj);
        } else {
            it->second = v;
        }
    }
}

// =====================================================================
//   Path lookup — dotted/bracketed traversal (`"a.b[0].c"`).
// =====================================================================

namespace detail {

enum class PathSegmentKind {
    Key,
    Index,
    End,
    Malformed,
};

struct PathSegment {
    PathSegmentKind kind = PathSegmentKind::End;
    std::string_view key;
    std::size_t index = 0;
};

// Pulls one segment from `path` starting at `pos` and updates `pos` past it.
// On success returns Key or Index. Returns End when the path is exhausted.
// Returns Malformed for unparsable input (e.g. unmatched bracket).
inline PathSegment next_segment(const std::string_view path, std::size_t& pos) {
    PathSegment out;
    if (pos >= path.size()) {
        out.kind = PathSegmentKind::End;
        return out;
    }

    const char c = path[pos];

    if (c == '[') {
        ++pos;
        const std::size_t start = pos;
        while (pos < path.size() && path[pos] != ']') {
            if (path[pos] < '0' || path[pos] > '9') {
                out.kind = PathSegmentKind::Malformed;
                return out;
            }
            ++pos;
        }
        if (pos >= path.size() || pos == start) {
            // Either missing ']' or empty []
            out.kind = PathSegmentKind::Malformed;
            return out;
        }
        std::size_t idx = 0;
        for (std::size_t i = start; i < pos; ++i) {
            idx = (idx * 10) + static_cast<std::size_t>(path[i] - '0');
        }
        ++pos;  // skip ']'
        out.kind = PathSegmentKind::Index;
        out.index = idx;
        return out;
    }

    if (c == '.') {
        // A leading '.' is malformed; a trailing '.' before next segment is also
        // unusual. We consume the dot and expect a key to follow.
        ++pos;
        if (pos >= path.size() || path[pos] == '.' || path[pos] == '[') {
            out.kind = PathSegmentKind::Malformed;
            return out;
        }
        return next_segment(path, pos);
    }

    // Read an identifier segment until '.' or '['.
    const std::size_t start = pos;
    while (pos < path.size() && path[pos] != '.' && path[pos] != '[') {
        ++pos;
    }
    out.kind = PathSegmentKind::Key;
    out.key = path.substr(start, pos - start);
    return out;
}

// Returns nullptr on miss, sets `malformed` to true on bad syntax.
inline const Value* walk_path(const Object& root, const std::string_view path, bool& malformed) {
    malformed = false;
    if (path.empty()) {
        // The root itself isn't a Value — but for symmetry, callers can
        // interpret an empty path as "the object". We return nullptr to
        // signal "no Value to return" and let callers handle root specially.
        return nullptr;
    }

    std::size_t pos = 0;
    PathSegment seg = next_segment(path, pos);
    if (seg.kind == PathSegmentKind::Malformed) {
        malformed = true;
        return nullptr;
    }
    if (seg.kind != PathSegmentKind::Key) {
        // Path must start with an identifier (root is an Object).
        malformed = true;
        return nullptr;
    }

    const Value* cur = root.find_ptr(seg.key);
    if (cur == nullptr) {
        return nullptr;
    }

    while (true) {
        seg = next_segment(path, pos);
        if (seg.kind == PathSegmentKind::End) {
            return cur;
        }
        if (seg.kind == PathSegmentKind::Malformed) {
            malformed = true;
            return nullptr;
        }
        if (seg.kind == PathSegmentKind::Key) {
            const Object* obj = cur->as_object_if();
            if (obj == nullptr) {
                malformed = true;  // type mismatch — require_path() will throw TypeError
                return nullptr;
            }
            cur = obj->find_ptr(seg.key);
            if (cur == nullptr) {
                return nullptr;
            }
        } else {  // Index
            const Array* arr = cur->as_array_if();
            if (arr == nullptr) {
                malformed = true;
                return nullptr;
            }
            if (seg.index >= arr->size()) {
                return nullptr;
            }
            cur = &(*arr)[seg.index];
        }
    }
}

inline Value* walk_path_mut(Object& root, const std::string_view path, bool& malformed) {
    // Reuse the const walker by stripping const off the result — the underlying
    // storage is non-const (we have a non-const Object&), so this is safe.
    const Object& croot = root;
    const Value* p = walk_path(croot, path, malformed);
    return const_cast<Value*>(p);  // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

}  // namespace detail

inline const Value* Object::find_path(const std::string_view path) const {
    bool malformed = false;
    return detail::walk_path(*this, path, malformed);
}

inline Value* Object::find_path(const std::string_view path) {
    bool malformed = false;
    return detail::walk_path_mut(*this, path, malformed);
}

inline const Value& Object::require_path(const std::string_view path) const {
    bool malformed = false;
    if (const Value* p = detail::walk_path(*this, path, malformed); p != nullptr) {
        return *p;
    }
    if (malformed) {
        throw TypeError("Object::require_path: malformed path or type mismatch: '" +
                        std::string(path) + "'");
    }
    throw MissingKeyError("Object::require_path: not found: '" + std::string(path) + "'");
}

inline Value& Object::require_path(const std::string_view path) {
    bool malformed = false;
    if (Value* p = detail::walk_path_mut(*this, path, malformed); p != nullptr) {
        return *p;
    }
    if (malformed) {
        throw TypeError("Object::require_path: malformed path or type mismatch: '" +
                        std::string(path) + "'");
    }
    throw MissingKeyError("Object::require_path: not found: '" + std::string(path) + "'");
}

inline bool Object::contains_path(const std::string_view path) const {
    bool malformed = false;
    return detail::walk_path(*this, path, malformed) != nullptr;
}

// =====================================================================
//   Free-function helpers — functional counterparts to Object methods.
// =====================================================================

/// True if `key` is present in `o`.
[[nodiscard]] inline bool contains(const Object& o, const std::string_view key) {
    return o.contains(key);
}

/// Return a pointer to the value for `key`, or `nullptr` if absent.
[[nodiscard]] inline Value* find_ptr(Object& o, const std::string_view key) {
    return o.find_ptr(key);
}
/// Return a pointer to the value for `key`, or `nullptr` if absent.
[[nodiscard]] inline const Value* find_ptr(const Object& o, const std::string_view key) {
    return o.find_ptr(key);
}

/// Return the value for `key`; throws `MissingKeyError` if absent.
inline Value& require(Object& o, const std::string_view key) {
    return o.require(key);
}
/// Return the value for `key`; throws `MissingKeyError` if absent.
inline const Value& require(const Object& o, const std::string_view key) {
    return o.require(key);
}

/// Return the string at `key`; throws on missing key or type mismatch.
inline std::string& require_string(Object& o, const std::string_view key) {
    return o.require_string(key);
}
/// Return the string at `key`; throws on missing key or type mismatch.
inline const std::string& require_string(const Object& o, const std::string_view key) {
    return o.require_string(key);
}

/// Return the array at `key`; throws on missing key or type mismatch.
inline Array& require_array(Object& o, const std::string_view key) {
    return o.require_array(key);
}
/// Return the array at `key`; throws on missing key or type mismatch.
inline const Array& require_array(const Object& o, const std::string_view key) {
    return o.require_array(key);
}

/// Return the nested object at `key`; throws on missing key or type mismatch.
inline Object& require_object(Object& o, const std::string_view key) {
    return o.require_object(key);
}
/// Return the nested object at `key`; throws on missing key or type mismatch.
inline const Object& require_object(const Object& o, const std::string_view key) {
    return o.require_object(key);
}

/// Pointer to the string at `key`, or `nullptr` if absent or wrong type.
[[nodiscard]] inline const std::string* get_string_if(const Object& o, const std::string_view key) {
    return o.get_string_if(key);
}
/// Pointer to the array at `key`, or `nullptr` if absent or wrong type.
[[nodiscard]] inline const Array* get_array_if(const Object& o, const std::string_view key) {
    return o.get_array_if(key);
}
/// Pointer to the nested object at `key`, or `nullptr` if absent or wrong type.
[[nodiscard]] inline const Object* get_object_if(const Object& o, const std::string_view key) {
    return o.get_object_if(key);
}

/// Deep-merge `src` into `dst`; see `Object::merge`.
inline void merge(Object& dst, const Object& src) {
    dst.merge(src);
}

// =====================================================================
//   Compact-JSON output — hand-rolled writers (no nlohmann dependency).
// =====================================================================

namespace detail {

inline void write_json_string(std::ostream& os, const std::string_view s) {
    os.put('"');
    for (const char c : s) {
        switch (c) {
        case '"':
            os << "\\\"";
            break;
        case '\\':
            os << "\\\\";
            break;
        case '\b':
            os << "\\b";
            break;
        case '\f':
            os << "\\f";
            break;
        case '\n':
            os << "\\n";
            break;
        case '\r':
            os << "\\r";
            break;
        case '\t':
            os << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                static constexpr std::array<char, 16> hex = {
                    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
                os << "\\u00";
                os.put(hex[(static_cast<unsigned char>(c) >> 4U) & 0xFU]);
                os.put(hex[static_cast<unsigned char>(c) & 0xFU]);
            } else {
                os.put(c);
            }
        }
    }
    os.put('"');
}

// Shortest round-trip decimal via std::to_chars: produces the fewest digits
// that, when parsed back to the same type, recover the exact same value.
// So `3.14` (a double) prints as "3.14", not "3.1400000000000001".
template <class T>
inline void write_json_floating(std::ostream& os, const T v) {
    static_assert(std::is_floating_point_v<T>);
    std::array<char, 64> buf{};
    auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v);
    (void)ec;  // 64 bytes is enough for any IEEE 754 binary32/binary64 shortest form
    os.write(buf.data(), end - buf.data());
}

void write_json(std::ostream& os, const Value& v);

inline void write_json(std::ostream& os, const Array& a) {
    os.put('[');
    bool first = true;
    for (const auto& el : a) {
        if (!first) {
            os.put(',');
        }
        first = false;
        write_json(os, el);
    }
    os.put(']');
}

inline void write_json(std::ostream& os, const Object& o) {
    os.put('{');
    bool first = true;
    for (const auto& [k, val] : o) {
        if (!first) {
            os.put(',');
        }
        first = false;
        write_json_string(os, k);
        os.put(':');
        write_json(os, val);
    }
    os.put('}');
}

inline void write_json(std::ostream& os, const Value& v) {
    std::visit(
        [&](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                os << "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                os << (x ? "true" : "false");
            } else if constexpr (std::is_same_v<T, std::int64_t> ||
                                 std::is_same_v<T, std::uint64_t>) {
                os << x;
            } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
                write_json_floating(os, x);
            } else if constexpr (std::is_same_v<T, std::string>) {
                write_json_string(os, x);
            } else if constexpr (std::is_same_v<T, Array>) {
                write_json(os, x);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<Object>>) {
                write_json(os, *x);
            }
        },
        v.raw());
}

}  // namespace detail

/// Stream `v` as compact JSON to `os`.
inline std::ostream& operator<<(std::ostream& os, const Value& v) {
    detail::write_json(os, v);
    return os;
}
/// Stream `o` as compact JSON to `os`.
inline std::ostream& operator<<(std::ostream& os, const Object& o) {
    detail::write_json(os, o);
    return os;
}
/// Stream `a` as compact JSON to `os`.
inline std::ostream& operator<<(std::ostream& os, const Array& a) {
    detail::write_json(os, a);
    return os;
}

namespace detail {

template <class T>
struct CompactJsonFormatter {
    template <class ParseCtx>
    constexpr auto parse(ParseCtx& ctx) -> typename ParseCtx::iterator {
        auto it = ctx.begin();
        auto end = ctx.end();
        if (it != end && *it != '}') {
            throw std::format_error(
                "metadata formatter: only the default (empty) format spec is supported");
        }
        return it;
    }

    template <class FormatCtx>
    auto format(const T& v, FormatCtx& ctx) const -> typename FormatCtx::iterator {
        std::ostringstream os;
        detail::write_json(os, v);
        const std::string s = os.str();
        return std::ranges::copy(s, ctx.out()).out;
    }
};

}  // namespace detail

}  // namespace comms::md

// =====================================================================
//   Root-level DX alias — `comms::Metadata` for the document-root case.
// =====================================================================

namespace comms {

/// Convenience alias surfacing the headline document-root type at the Commons
/// root: `comms::Metadata` is `comms::md::Object`. Everything else stays under
/// `comms::md::…`.
using Metadata = md::Object;

}  // namespace comms

// =====================================================================
//   std::hash specializations (stable within a single process).
// =====================================================================

namespace comms::md::detail {

inline std::size_t hash_combine(const std::size_t seed, const std::size_t v) noexcept {
    // Boost-style mix; documented as stable within a process only.
    return seed ^ (v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

// Per-alternative salt — keeps Value{0} (int), Value{0u} (uint), Value{0.0}
// (double), and Value{false} from colliding.
constexpr std::size_t alt_salt(const std::size_t idx) noexcept {
    constexpr std::size_t base = 0xcbf29ce484222325ULL;
    return base + (idx * 0x100000001b3ULL);
}

std::size_t hash_value(const Value& v) noexcept;

inline std::size_t hash_array(const Array& a) noexcept {
    std::size_t h = alt_salt(7);
    for (const auto& el : a) {
        h = hash_combine(h, hash_value(el));
    }
    return h;
}

// Order-independent so two Objects that compare equal hash equal regardless
// of insertion order.
inline std::size_t hash_object(const Object& o) noexcept {
    std::size_t acc = 0;
    for (const auto& [k, val] : o) {
        std::size_t entry = std::hash<std::string_view>{}(std::string_view{k});
        entry = hash_combine(entry, hash_value(val));
        acc ^= entry;
    }
    // Mix in a non-zero salt so an empty object hashes distinctly from "all
    // entry-hashes XORed to zero by accident".
    return acc ^ alt_salt(8);
}

inline std::size_t hash_value(const Value& v) noexcept {
    return std::visit(
        [&](const auto& x) noexcept -> std::size_t {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                return alt_salt(0);
            } else if constexpr (std::is_same_v<T, bool>) {
                return hash_combine(alt_salt(1), std::hash<bool>{}(x));
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return hash_combine(alt_salt(2), std::hash<std::int64_t>{}(x));
            } else if constexpr (std::is_same_v<T, std::uint64_t>) {
                return hash_combine(alt_salt(3), std::hash<std::uint64_t>{}(x));
            } else if constexpr (std::is_same_v<T, float>) {
                return hash_combine(alt_salt(4), std::hash<float>{}(x));
            } else if constexpr (std::is_same_v<T, double>) {
                return hash_combine(alt_salt(5), std::hash<double>{}(x));
            } else if constexpr (std::is_same_v<T, std::string>) {
                return hash_combine(alt_salt(6),
                                    std::hash<std::string_view>{}(std::string_view{x}));
            } else if constexpr (std::is_same_v<T, Array>) {
                return hash_array(x);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<Object>>) {
                return hash_object(*x);
            } else {
                return 0;
            }
        },
        v.raw());
}

}  // namespace comms::md::detail

/// `std::hash` specialization for `comms::md::Value` (stable within a single process).
template <>
struct std::hash<comms::md::Value> {
    /// Hash a `comms::md::Value`.
    std::size_t operator()(const comms::md::Value& v) const noexcept {
        return comms::md::detail::hash_value(v);
    }
};

/// `std::hash` specialization for `comms::md::Object`; order-independent so
/// equal objects hash equal regardless of insertion order.
template <>
struct std::hash<comms::md::Object> {
    /// Hash a `comms::md::Object`.
    std::size_t operator()(const comms::md::Object& o) const noexcept {
        return comms::md::detail::hash_object(o);
    }
};

/// `std::hash` specialization for `comms::md::Array`.
template <>
struct std::hash<comms::md::Array> {
    /// Hash a `comms::md::Array`.
    std::size_t operator()(const comms::md::Array& a) const noexcept {
        return comms::md::detail::hash_array(a);
    }
};

// =====================================================================
//   std::formatter specializations — compact JSON.
// =====================================================================

/// `std::format` specialization that emits a `comms::md::Value` as compact JSON.
template <>
struct std::formatter<comms::md::Value, char>
    : comms::md::detail::CompactJsonFormatter<comms::md::Value> {};

/// `std::format` specialization that emits a `comms::md::Object` as compact JSON.
template <>
struct std::formatter<comms::md::Object, char>
    : comms::md::detail::CompactJsonFormatter<comms::md::Object> {};

/// `std::format` specialization that emits a `comms::md::Array` as compact JSON.
template <>
struct std::formatter<comms::md::Array, char>
    : comms::md::detail::CompactJsonFormatter<comms::md::Array> {};
