#pragma once

/// @file
/// @brief `comms::LockFreeQueue` — a lock-free multi-producer / multi-consumer
///        FIFO queue, the first `std::atomic`-based type in Commons.
///
/// The algorithm is the classic **Michael–Scott** queue: a singly linked list
/// with a dummy sentinel node (so the list is never structurally empty),
/// `push` links a new node at the tail, `pop` unlinks at the head and returns
/// the value carried by the *new* head. ABA on the link words is defeated by
/// bundling a monotonically bumped **tag** into every link, so a stale CAS that
/// observes a recycled node sees a different tag and fails.
///
/// Two modes share that logic and differ only in the **link representation** and
/// **where nodes come from** (factored into a `detail` storage policy):
///
///   - `LockFreeQueueMode::Fixed` — fully portable, no double-width CAS. Nodes
///     live in one preallocated array of `capacity + 1` slots; every link is a
///     `std::atomic<comms::u64>` packing a 32-bit slot **index** + 32-bit
///     **tag**. `std::atomic<u64>` is lock-free on every supported target with
///     no special flags. `push` returns `false` when the slots are exhausted
///     (the queue is full).
///
///   - `LockFreeQueueMode::Dynamic` (the default) — unbounded. Nodes are
///     heap-allocated and recycled through a lock-free
///     freelist; links are a 16-byte `std::atomic<TaggedPtr>` (`{Node*, tag}`)
///     updated by a 128-bit DWCAS. **Portability caveat:** on x86-64 this is
///     lock-free only when compiled with `-mcx16` (and may need `libatomic` on
///     GCC/Linux); without it the standard library falls back to a hidden lock
///     — still correct, just not lock-free. On arm64 (Apple Silicon) the 16-byte
///     atomic is lock-free natively. Use `is_lock_free()` to check at runtime.
///     The `capacity` ctor argument is the initial freelist reserve; the queue
///     still grows past it by allocating more nodes.
///
/// **Element restriction.** `T` must satisfy `comms::LockFreeValue` (trivially
/// copyable, trivially destructible, nothrow copy/move constructible). Values
/// are copied in and out by trivial copy; the node pool never runs `T`'s
/// destructor.
///
/// **memory_order strategy.** Standard correct M–S orderings, explicit
/// everywhere (no implicit `seq_cst`): loads of head/tail/next are `acquire`;
/// the CAS that publishes a link (linking a new node, swinging head/tail) is
/// `release` on success and `relaxed`/`acquire` on the failure reload. The
/// freelist link that publishes a recycled node is `release` and its matching
/// load `acquire`. The ABA tag rides in the same CAS word (data, not
/// synchronization) and needs no separate fence; CAS loops use
/// `compare_exchange_weak`. The element itself is held in a `std::atomic<T>` and
/// transferred with `relaxed` load/store: ordering is already carried by the
/// link CAS, but a consumer reads a node's value *before* the CAS that validates
/// the pop, which can overlap a producer recycling that slot — harmless on
/// hardware (the value is discarded when the CAS fails) but a data race in the
/// abstract machine, so the access is atomic to keep it well-defined (and clean
/// under ThreadSanitizer). For a `T` small enough to be lock-free this stays
/// fully lock-free; `is_lock_free()` folds that in.
///
/// **Guarantee.** A value successfully `push`ed *happens-before* the `pop` that
/// returns it. `empty()` is a racy best-effort snapshot — meaningful only when
/// the caller knows there is no concurrent access — and there is deliberately no
/// exact `size()` (not obtainable lock-free).
///
/// **No JSON (deliberate exception to the per-type-serialization rule).** A
/// concurrent queue is mutable shared state: a snapshot is racy and a round-trip
/// meaningless, so there is no `commons/json/lock_free_queue.hpp`. This mirrors
/// `lifecycle.hpp`, which keeps JSON on its value records, not on the
/// synchronizing object.

#include <commons/types.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

// ---------------------------------------------------------------------------
// Configurable default capacity for LockFreeQueue (value-override seam).
//
// Lives here rather than in commons/config.hpp (reserved for the boolean
// COMMONS_WITH_* integration gates), mirroring COMMONS_AUDIT_RECORDS_CAPACITY
// in audit_record.hpp. A build system or consumer may predefine it before this
// header is first included; CMake/Meson emit a -D only when overridden.
// ---------------------------------------------------------------------------
#if !defined(COMMONS_LOCK_FREE_QUEUE_DEFAULT_CAPACITY)
#define COMMONS_LOCK_FREE_QUEUE_DEFAULT_CAPACITY 1024
#endif

namespace comms {

/// The element restriction for a lock-free node pool: values must be trivially
/// copyable / destructible (the pool copies them with raw stores and never
/// destructs them) and nothrow constructible (a throwing copy mid-CAS-loop would
/// break the lock-free progress guarantee).
template <typename T>
concept LockFreeValue =
    std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T> &&
    std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_move_constructible_v<T>;

/// Selects the queue's link representation and node-allocation strategy.
enum class LockFreeQueueMode {
    Dynamic,  ///< Unbounded; heap nodes + 128-bit tagged-pointer DWCAS.
    Fixed,    ///< Bounded; array nodes + 64-bit packed index/tag CAS.
};

namespace detail {

// Fixed-capacity storage: nodes in one preallocated array, links are a
// std::atomic<u64> packing a 32-bit slot index (low) + 32-bit ABA tag (high).
// The freelist is a Treiber stack threaded through the nodes' own `next` field.
template <typename T>
class LfqFixedStorage {
public:
    using Handle = u64;

    struct Node {
        std::atomic<Handle> next;
        // The value is carried through an atomic so the consumer's
        // read-before-validating-CAS cannot data-race a producer that recycles
        // this slot (a benign race on real hardware, but UB and TSan-flagged).
        // Ordering is still carried by the `next` links; this access is relaxed.
        std::atomic<T> data{};
    };

    explicit LfqFixedStorage(const u64 capacity) : capacity_(capacity) {
        // One extra slot for the dummy sentinel the queue always holds.
        const u64 slots = capacity + 1;
        // unique_ptr<Node[]> is the right tool for a runtime-sized array of
        // non-movable atomics: std::vector needs a movable element, std::array a
        // compile-time size. NOLINT the C-array idiom this requires.
        // NOLINTNEXTLINE(modernize-avoid-c-arrays)
        nodes_ = std::make_unique<Node[]>(slots);
        free_.store(make_handle(nullptr, 0), std::memory_order_relaxed);
        for (u64 i = 0; i < slots; ++i) {
            free_node(&nodes_[i]);
        }
    }

    [[nodiscard]] Node* get_pointer(const Handle h) const noexcept {
        const auto idx = static_cast<u32>(h & 0xFFFFFFFFu);
        return idx == null_index ? nullptr : &nodes_[idx];
    }
    [[nodiscard]] static u32 get_tag(const Handle h) noexcept {
        return static_cast<u32>(h >> 32);
    }
    [[nodiscard]] Handle make_handle(Node* node, const u32 tag) const noexcept {
        const u32 idx = node ? static_cast<u32>(node - nodes_.get()) : null_index;
        return (static_cast<u64>(tag) << 32) | idx;
    }

    // Pop a free slot (queue full -> nullptr). Fixed mode never grows.
    [[nodiscard]] Node* allocate_node() noexcept {
        return pop_free();
    }
    void free_node(Node* p) noexcept {
        push_free(p);
    }

    [[nodiscard]] u64 capacity() const noexcept {
        return capacity_;
    }
    [[nodiscard]] bool is_lock_free() const noexcept {
        return free_.is_lock_free();
    }

private:
    static constexpr u32 null_index = 0xFFFFFFFFu;

    [[nodiscard]] Node* pop_free() noexcept {
        Handle old = free_.load(std::memory_order_acquire);
        for (;;) {
            Node* p = get_pointer(old);
            if (p == nullptr) {
                return nullptr;
            }
            const Handle next = p->next.load(std::memory_order_acquire);
            if (const Handle neu = make_handle(get_pointer(next), get_tag(old) + 1);
                free_.compare_exchange_weak(
                    old, neu, std::memory_order_acq_rel, std::memory_order_acquire)) {
                return p;
            }
        }
    }

    void push_free(Node* p) noexcept {
        Handle old = free_.load(std::memory_order_acquire);
        for (;;) {
            p->next.store(old, std::memory_order_relaxed);
            if (const Handle neu = make_handle(p, get_tag(old) + 1); free_.compare_exchange_weak(
                    old, neu, std::memory_order_release, std::memory_order_acquire)) {
                return;
            }
        }
    }

    // NOLINTNEXTLINE(modernize-avoid-c-arrays) — runtime-sized array of atomics.
    std::unique_ptr<Node[]> nodes_;
    std::atomic<Handle> free_;
    u64 capacity_;
};

// Dynamic (unbounded) storage: heap nodes recycled through a lock-free freelist,
// links are a 16-byte std::atomic<TaggedPtr> updated by 128-bit DWCAS. Every
// allocated node is threaded onto a set-once push-only stack so the destructor
// (which runs single-threaded) can delete them all.
template <typename T>
class LfqDynamicStorage {
public:
    struct Node;

    struct alignas(16) TaggedPtr {
        Node* ptr = nullptr;
        std::uintptr_t tag = 0;
        friend bool operator==(const TaggedPtr&, const TaggedPtr&) = default;
    };

    using Handle = TaggedPtr;

    struct Node {
        std::atomic<Handle> next;
        std::atomic<T> data{};     // see LfqFixedStorage::Node — atomic for race-freedom
        Node* all_next = nullptr;  // set-once link for destruction-time cleanup
    };

    explicit LfqDynamicStorage(const u64 reserve) {
        free_.store(Handle{}, std::memory_order_relaxed);
        all_head_.store(nullptr, std::memory_order_relaxed);
        for (u64 i = 0; i < reserve; ++i) {
            Node* n = create_raw();
            if (n == nullptr) {
                break;
            }
            free_node(n);
        }
    }

    LfqDynamicStorage(const LfqDynamicStorage&) = delete;
    LfqDynamicStorage& operator=(const LfqDynamicStorage&) = delete;
    LfqDynamicStorage(LfqDynamicStorage&&) = delete;
    LfqDynamicStorage& operator=(LfqDynamicStorage&&) = delete;

    // The freelist threads raw Node* through std::atomic; a smart pointer can't
    // live inside the atomic links, so this storage owns its nodes by hand
    // (allocated in create_raw, freed here at single-threaded destruction).
    // NOLINTBEGIN(cppcoreguidelines-owning-memory)
    ~LfqDynamicStorage() {
        Node* p = all_head_.load(std::memory_order_relaxed);
        while (p != nullptr) {
            Node* next = p->all_next;
            delete p;
            p = next;
        }
    }
    // NOLINTEND(cppcoreguidelines-owning-memory)

    [[nodiscard]] Node* get_pointer(Handle h) const noexcept {
        return h.ptr;
    }
    [[nodiscard]] static std::uintptr_t get_tag(Handle h) noexcept {
        return h.tag;
    }
    [[nodiscard]] Handle make_handle(Node* node, std::uintptr_t tag) const noexcept {
        return Handle{node, tag};
    }

    // Pop a recycled node, or grow by allocating (unbounded). nullptr only when
    // a heap allocation fails.
    [[nodiscard]] Node* allocate_node() noexcept {
        Node* p = pop_free();
        return p != nullptr ? p : create_raw();
    }
    void free_node(Node* p) noexcept {
        push_free(p);
    }

    [[nodiscard]] bool is_lock_free() const noexcept {
        return free_.is_lock_free();
    }

private:
    [[nodiscard]] Node* create_raw() noexcept {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) — hand-owned pool node.
        Node* n = new (std::nothrow) Node;
        if (n == nullptr) {
            return nullptr;
        }
        // Register on the set-once all-nodes stack (push-only, never CAS'd back).
        Node* head = all_head_.load(std::memory_order_relaxed);
        do {
            n->all_next = head;
        } while (!all_head_.compare_exchange_weak(
            head, n, std::memory_order_release, std::memory_order_relaxed));
        return n;
    }

    [[nodiscard]] Node* pop_free() noexcept {
        Handle old = free_.load(std::memory_order_acquire);
        for (;;) {
            Node* p = old.ptr;
            if (p == nullptr) {
                return nullptr;
            }
            const Handle next = p->next.load(std::memory_order_acquire);
            if (const Handle neu{next.ptr, old.tag + 1}; free_.compare_exchange_weak(
                    old, neu, std::memory_order_acq_rel, std::memory_order_acquire)) {
                return p;
            }
        }
    }

    void push_free(Node* p) noexcept {
        Handle old = free_.load(std::memory_order_acquire);
        for (;;) {
            p->next.store(old, std::memory_order_relaxed);
            if (const Handle neu{p, old.tag + 1}; free_.compare_exchange_weak(
                    old, neu, std::memory_order_release, std::memory_order_acquire)) {
                return;
            }
        }
    }

    std::atomic<Handle> free_;
    std::atomic<Node*> all_head_;
};

template <LockFreeQueueMode Mode, typename T>
using LfqStorageFor =
    std::conditional_t<Mode == LockFreeQueueMode::Fixed, LfqFixedStorage<T>, LfqDynamicStorage<T>>;

}  // namespace detail

/// A lock-free MPMC FIFO queue (Michael–Scott). See the file `@brief` for the
/// algorithm, the two modes' ABA schemes, the `-mcx16` caveat for `Dynamic` on
/// x86-64, and the happens-before guarantee.
///
/// @tparam T    the element type; must satisfy `comms::LockFreeValue`.
/// @tparam Mode `Dynamic` (unbounded, default) or `Fixed` (bounded).
template <LockFreeValue T, LockFreeQueueMode Mode = LockFreeQueueMode::Dynamic>
class LockFreeQueue {
    using Storage = detail::LfqStorageFor<Mode, T>;
    using Node = typename Storage::Node;
    using Handle = typename Storage::Handle;

public:
    using value_type = T;
    using size_type = u64;

    /// Fixed-mode default capacity (also the Dynamic initial reserve). Overridable
    /// at build time via the `COMMONS_LOCK_FREE_QUEUE_DEFAULT_CAPACITY` seam.
    //
    // SCREAMING_CASE is intentional — a public capacity constant mirroring its
    // override macro, like comms::Prioritized's sentinels.
    // NOLINTNEXTLINE(readability-identifier-naming)
    static constexpr size_type DEFAULT_CAPACITY = COMMONS_LOCK_FREE_QUEUE_DEFAULT_CAPACITY;

    /// In `Fixed` mode `capacity` is the bound. In `Dynamic` mode it is the
    /// initial freelist reserve (the queue still grows past it).
    explicit LockFreeQueue(size_type capacity = DEFAULT_CAPACITY) : storage_(capacity) {
        Node* dummy = storage_.allocate_node();  // always available: reserve >= 1
        dummy->next.store(storage_.make_handle(nullptr, 0), std::memory_order_relaxed);
        const Handle h = storage_.make_handle(dummy, 0);
        head_.store(h, std::memory_order_relaxed);
        tail_.store(h, std::memory_order_relaxed);
    }

    ~LockFreeQueue() = default;

    // Owns atomics + a node pool — non-copyable, non-movable (mirrors
    // StatusTransitionTimeline in lifecycle.hpp).
    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
    LockFreeQueue(LockFreeQueue&&) = delete;
    LockFreeQueue& operator=(LockFreeQueue&&) = delete;

    /// Enqueue a copy of `value`. Returns `false` in `Fixed` mode when the queue
    /// is full, or in `Dynamic` mode only if a node allocation fails.
    [[nodiscard]] bool push(const T& value) noexcept {
        Node* node = storage_.allocate_node();
        if (node == nullptr) {
            return false;
        }
        node->data.store(value, std::memory_order_relaxed);
        node->next.store(storage_.make_handle(nullptr, 0), std::memory_order_relaxed);

        for (;;) {
            Handle tail = tail_.load(std::memory_order_acquire);
            Node* tail_node = storage_.get_pointer(tail);
            Handle next = tail_node->next.load(std::memory_order_acquire);
            // Re-read tail to confirm a consistent (tail, next) snapshot.
            if (tail != tail_.load(std::memory_order_acquire)) {
                continue;
            }
            Node* next_node = storage_.get_pointer(next);
            if (next_node == nullptr) {
                // Tail is the real last node: link the new node after it.
                if (const Handle new_next = storage_.make_handle(node, Storage::get_tag(next) + 1);
                    tail_node->next.compare_exchange_weak(
                        next, new_next, std::memory_order_release, std::memory_order_relaxed)) {
                    // Linked — try to swing the tail forward (best-effort).
                    const Handle new_tail = storage_.make_handle(node, Storage::get_tag(tail) + 1);
                    tail_.compare_exchange_strong(
                        tail, new_tail, std::memory_order_release, std::memory_order_relaxed);
                    return true;
                }
            } else {
                // Tail is lagging behind a linked node: help advance it.
                const Handle new_tail = storage_.make_handle(next_node, Storage::get_tag(tail) + 1);
                tail_.compare_exchange_strong(
                    tail, new_tail, std::memory_order_release, std::memory_order_relaxed);
            }
        }
    }

    /// Move-overload of `push`. `T` is trivially copyable, so this is equivalent
    /// to the copy overload — provided for symmetry with the standard containers
    /// and so generic code that forwards an rvalue resolves cleanly. The rvalue
    /// is intentionally copied, not moved: a move of a trivially-copyable type is
    /// a copy.
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    [[nodiscard]] bool push(T&& value) noexcept {
        return push(static_cast<const T&>(value));
    }

    /// Construct a `T` in place from `args` and enqueue it. Returns `false` under
    /// the same conditions as `push` (Fixed: full; Dynamic: allocation failed).
    /// The value is materialized on the stack first (the node holds an
    /// `std::atomic<T>`, so there is no in-node construction), then pushed.
    template <typename... Args>
    [[nodiscard]] bool
    emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        return push(T(std::forward<Args>(args)...));
    }

    /// Dequeue into `out`. Returns `false` when the queue is empty.
    [[nodiscard]] bool pop(T& out) noexcept {
        for (;;) {
            Handle head = head_.load(std::memory_order_acquire);
            Node* head_node = storage_.get_pointer(head);
            Handle tail = tail_.load(std::memory_order_acquire);
            const Handle next = head_node->next.load(std::memory_order_acquire);
            // Re-read head to confirm a consistent snapshot.
            if (head != head_.load(std::memory_order_acquire)) {
                continue;
            }
            Node* next_node = storage_.get_pointer(next);
            if (head_node == storage_.get_pointer(tail)) {
                if (next_node == nullptr) {
                    return false;  // empty
                }
                // Tail lagging behind a linked node: help advance it, then retry.
                const Handle new_tail = storage_.make_handle(next_node, Storage::get_tag(tail) + 1);
                tail_.compare_exchange_strong(
                    tail, new_tail, std::memory_order_release, std::memory_order_relaxed);
            } else {
                if (next_node == nullptr) {
                    continue;  // transient inconsistency, retry
                }
                // Read the value before swinging head; the acquire load of `next`
                // synchronizes-with the producer's release store of this link.
                T value = next_node->data.load(std::memory_order_relaxed);
                if (const Handle new_head =
                        storage_.make_handle(next_node, Storage::get_tag(head) + 1);
                    head_.compare_exchange_weak(
                        head, new_head, std::memory_order_release, std::memory_order_relaxed)) {
                    out = value;
                    storage_.free_node(head_node);  // recycle the old dummy
                    return true;
                }
            }
        }
    }

    /// Dequeue and return the value, or `std::nullopt` when empty — a convenience
    /// wrapper over `pop(T&)`. Returning by value (not a reference) is deliberate:
    /// a popped node may be recycled immediately, so there is nothing safe to
    /// reference.
    [[nodiscard]] std::optional<T> try_pop() noexcept {
        if (T value{}; pop(value)) {
            return value;
        }
        return std::nullopt;
    }

    /// Racy best-effort emptiness check — meaningful only with no concurrent
    /// access. There is deliberately no exact `size()`.
    [[nodiscard]] bool empty() const noexcept {
        const Handle head = head_.load(std::memory_order_acquire);
        Node* head_node = storage_.get_pointer(head);
        const Handle next = head_node->next.load(std::memory_order_acquire);
        return storage_.get_pointer(next) == nullptr;
    }

    /// `Fixed`: the configured bound. `Dynamic`: `0` (unbounded).
    [[nodiscard]] size_type capacity() const noexcept {
        if constexpr (Mode == LockFreeQueueMode::Fixed) {
            return storage_.capacity();
        } else {
            return 0;
        }
    }

    /// Whether the queue's operations are truly lock-free on this target (always
    /// true in `Fixed` mode; in `Dynamic` mode depends on 16-byte atomic support
    /// — see the `-mcx16` caveat in the file `@brief`).
    [[nodiscard]] bool is_lock_free() const noexcept {
        return head_.is_lock_free() && tail_.is_lock_free() && storage_.is_lock_free() &&
               std::atomic<T>::is_always_lock_free;
    }

private:
    Storage storage_;
    std::atomic<Handle> head_;
    std::atomic<Handle> tail_;
};

}  // namespace comms
