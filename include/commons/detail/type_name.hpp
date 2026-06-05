#pragma once

/// @file
/// @brief `comms::detail::demangle_type_name` — a human title for a
///        `std::type_info`: the demangled type name reduced to its final
///        identifier.
///
/// Shared by the polymorphic open-set value families whose default `title()` is
/// the concrete type's name (`comms::IReason`, `comms::IAbility`,
/// `comms::IIdentity`). It lives in its own header so each of those headers can
/// reuse the single definition — defining it inline in more than one header
/// would be a redefinition once the umbrella pulls them into one translation
/// unit.

#include <cstdlib>
#include <string>
#include <string_view>
#include <typeinfo>

#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#endif

namespace comms::detail {

/// A human title for `ti`: the demangled type name reduced to its final
/// identifier (enclosing namespaces — including `(anonymous namespace)` — and
/// any template arguments stripped).
[[nodiscard]] inline std::string demangle_type_name(const std::type_info& ti) {
    std::string name;
#if defined(__GNUC__) || defined(__clang__)
    int status = 0;
    // __cxa_demangle returns a malloc'd buffer we must free; the manual
    // malloc/free is intrinsic to the Itanium ABI and confined to these lines.
    // NOLINTBEGIN(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
    char* demangled = abi::__cxa_demangle(ti.name(), nullptr, nullptr, &status);
    name = (status == 0 && demangled != nullptr) ? demangled : ti.name();
    std::free(demangled);  // free(nullptr) is a no-op
    // NOLINTEND(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
#else
    name = ti.name();
    // MSVC prefixes the readable name with "class "/"struct ".
    for (const std::string_view prefix : {"class ", "struct "}) {
        if (name.starts_with(prefix)) {
            name.erase(0, prefix.size());
            break;
        }
    }
#endif
    if (const auto lt = name.find('<'); lt != std::string::npos) {
        name.erase(lt);  // drop template args (and any "::" inside them)
    }
    if (const auto pos = name.rfind("::"); pos != std::string::npos) {
        name.erase(0, pos + 2);  // drop namespace / enclosing-scope qualification
    }
    return name;
}

}  // namespace comms::detail
