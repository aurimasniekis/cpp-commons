#include <commons/lock_free_queue.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using comms::LockFreeQueue;
using comms::LockFreeQueueMode;
using comms::LockFreeValue;

// -- concept checks ----------------------------------------------------------

struct TrivialPod {
    [[maybe_unused]] int a;
    [[maybe_unused]] double b;
};

static_assert(LockFreeValue<int>);
static_assert(LockFreeValue<std::uint64_t>);
static_assert(LockFreeValue<TrivialPod>);
static_assert(!LockFreeValue<std::string>);

// -- typed fixture: run the single-threaded suite for both modes -------------

template <typename ModeTag>
class LockFreeQueueTyped : public ::testing::Test {
public:
    static constexpr LockFreeQueueMode mode = ModeTag::value;
    using Queue = LockFreeQueue<int, mode>;
};

struct DynamicTag {
    static constexpr auto value = LockFreeQueueMode::Dynamic;
};
struct FixedTag {
    static constexpr auto value = LockFreeQueueMode::Fixed;
};

using Modes = ::testing::Types<DynamicTag, FixedTag>;
TYPED_TEST_SUITE(LockFreeQueueTyped, Modes);

TYPED_TEST(LockFreeQueueTyped, DefaultCtorUsesDefaultCapacity) {
    typename TestFixture::Queue q;
    // DEFAULT_CAPACITY is the seam default (1024 unless overridden at build).
    EXPECT_EQ(TestFixture::Queue::DEFAULT_CAPACITY, COMMONS_LOCK_FREE_QUEUE_DEFAULT_CAPACITY);
    EXPECT_TRUE(q.empty());
}

TYPED_TEST(LockFreeQueueTyped, PopOnEmptyReturnsFalse) {
    typename TestFixture::Queue q(8);
    int out = -1;
    EXPECT_FALSE(q.pop(out));
    EXPECT_EQ(out, -1);  // left untouched
}

TYPED_TEST(LockFreeQueueTyped, PushPopFifoOrder) {
    typename TestFixture::Queue q(16);
    EXPECT_TRUE(q.empty());
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(q.push(i));
    }
    EXPECT_FALSE(q.empty());
    for (int i = 0; i < 10; ++i) {
        int out = -1;
        EXPECT_TRUE(q.pop(out));
        EXPECT_EQ(out, i);  // FIFO
    }
    EXPECT_TRUE(q.empty());
    int out = -1;
    EXPECT_FALSE(q.pop(out));
}

TYPED_TEST(LockFreeQueueTyped, EmptyTransitions) {
    typename TestFixture::Queue q(4);
    EXPECT_TRUE(q.empty());
    EXPECT_TRUE(q.push(1));
    EXPECT_FALSE(q.empty());
    int out = 0;
    EXPECT_TRUE(q.pop(out));
    EXPECT_TRUE(q.empty());
}

TYPED_TEST(LockFreeQueueTyped, PushMoveOverload) {
    typename TestFixture::Queue q(4);
    int v = 7;
    EXPECT_TRUE(q.push(v));  // const T&
    EXPECT_TRUE(q.push(8));  // T&& (rvalue temporary)
    EXPECT_TRUE(q.push(std::move(v)));
    int out = -1;
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(out, 7);
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(out, 8);
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(out, 7);
}

TYPED_TEST(LockFreeQueueTyped, Emplace) {
    LockFreeQueue<TrivialPod, TestFixture::mode> q(4);
    EXPECT_TRUE(q.emplace(3, 1.5));  // TrivialPod{3, 1.5}
    EXPECT_TRUE(q.emplace());        // value-initialized
    TrivialPod out{-1, -1.0};
    ASSERT_TRUE(q.pop(out));
    EXPECT_EQ(out.a, 3);
    EXPECT_EQ(out.b, 1.5);
    ASSERT_TRUE(q.pop(out));
    EXPECT_EQ(out.a, 0);
    EXPECT_EQ(out.b, 0.0);
}

TYPED_TEST(LockFreeQueueTyped, TryPop) {
    typename TestFixture::Queue q(4);
    EXPECT_FALSE(q.try_pop().has_value());  // empty
    EXPECT_TRUE(q.push(42));
    const auto v = q.try_pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
    EXPECT_FALSE(q.try_pop().has_value());  // drained
}

// Push/drain ~10x capacity interleaved, to flush out freelist/tag bugs.
TYPED_TEST(LockFreeQueueTyped, WraparoundRecycleStress) {
    constexpr int cap = 8;
    typename TestFixture::Queue q(cap);
    int expected = 0;
    for (int round = 0; round < 10; ++round) {
        for (int i = 0; i < cap; ++i) {
            ASSERT_TRUE(q.push(expected + i));
        }
        for (int i = 0; i < cap; ++i) {
            int out = -1;
            ASSERT_TRUE(q.pop(out));
            ASSERT_EQ(out, expected + i);
        }
        expected += cap;
    }
    EXPECT_TRUE(q.empty());
}

TYPED_TEST(LockFreeQueueTyped, IsLockFree) {
    typename TestFixture::Queue q(4);
    if constexpr (TestFixture::mode == LockFreeQueueMode::Fixed) {
        EXPECT_TRUE(q.is_lock_free());  // atomic<u64> — always lock-free
    } else {
        // Dynamic uses a 16-byte atomic: lock-free on arm64 / with -mcx16 on
        // x86-64, otherwise a hidden lock. Don't hard-assert; just record it.
        if (!q.is_lock_free()) {
            GTEST_LOG_(INFO) << "Dynamic LockFreeQueue is not lock-free on this "
                                "build (no 16-byte atomic / -mcx16).";
        }
    }
}

// -- mode-specific single-threaded behavior ----------------------------------

TEST(LockFreeQueueFixed, CapacityIsTheBound) {
    LockFreeQueue<int, LockFreeQueueMode::Fixed> q(4);
    EXPECT_EQ(q.capacity(), 4u);
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(q.push(i));
    }
    EXPECT_FALSE(q.push(99));  // full
    int out = -1;
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(out, 0);
    EXPECT_TRUE(q.push(100));  // a slot freed up
}

TEST(LockFreeQueueDynamic, CapacityReportsZeroAndGrowsPastReserve) {
    LockFreeQueue<int, LockFreeQueueMode::Dynamic> q(2);
    EXPECT_EQ(q.capacity(), 0u);  // unbounded
    // Push well past the initial reserve of 2 — it must grow.
    constexpr int n = 100;
    for (int i = 0; i < n; ++i) {
        ASSERT_TRUE(q.push(i));
    }
    for (int i = 0; i < n; ++i) {
        int out = -1;
        ASSERT_TRUE(q.pop(out));
        ASSERT_EQ(out, i);
    }
    EXPECT_TRUE(q.empty());
}

// -- concurrency stress ------------------------------------------------------

// N producers each push a disjoint tagged range; M consumers pop until the
// shared consumed-count reaches the total. Invariants: every produced value is
// consumed exactly once (no loss, no duplication) and totals match.
template <LockFreeQueueMode Mode>
void run_mpmc_stress(const unsigned producers,
                     const unsigned consumers,
                     const std::uint32_t per_producer,
                     typename LockFreeQueue<std::uint64_t, Mode>::size_type capacity) {
    using Queue = LockFreeQueue<std::uint64_t, Mode>;
    Queue q(capacity);

    const std::uint64_t total = static_cast<std::uint64_t>(producers) * per_producer;
    std::vector<std::atomic<int>> seen(total);  // value -> consumed count
    for (auto& s : seen) {
        s.store(0, std::memory_order_relaxed);
    }

    std::atomic<std::uint64_t> consumed{0};
    std::atomic<bool> go{false};

    auto producer_fn = [&](const std::uint32_t pid) {
        while (!go.load(std::memory_order_acquire)) {}
        for (std::uint32_t i = 0; i < per_producer; ++i) {
            // Encode (producer, index) into a globally unique value.
            const std::uint64_t value = static_cast<std::uint64_t>(pid) * per_producer + i;
            while (!q.push(value)) {
                // Fixed mode may be momentarily full — spin until a slot frees.
                std::this_thread::yield();
            }
        }
    };

    auto consumer_fn = [&]() {
        while (!go.load(std::memory_order_acquire)) {}
        while (consumed.load(std::memory_order_acquire) < total) {
            if (std::uint64_t out = 0; q.pop(out)) {
                ASSERT_LT(out, total);
                seen[out].fetch_add(1, std::memory_order_relaxed);
                consumed.fetch_add(1, std::memory_order_acq_rel);
            } else {
                std::this_thread::yield();
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(producers + consumers);
    for (unsigned c = 0; c < consumers; ++c) {
        threads.emplace_back(consumer_fn);
    }
    for (unsigned p = 0; p < producers; ++p) {
        threads.emplace_back(producer_fn, p);
    }
    go.store(true, std::memory_order_release);
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(consumed.load(), total);
    for (std::uint64_t v = 0; v < total; ++v) {
        EXPECT_EQ(seen[v].load(std::memory_order_relaxed), 1) << "value " << v;
    }
    std::uint64_t out = 0;
    std::uint64_t leftover = 0;
    while (q.pop(out)) {
        ++leftover;
    }
    EXPECT_EQ(leftover, 0u);
}

unsigned stress_threads() {
    const unsigned hw = std::thread::hardware_concurrency();
    return hw >= 8 ? 4u : 2u;
}

TEST(LockFreeQueueStress, DynamicMpmc) {
    const unsigned t = stress_threads();
    run_mpmc_stress<LockFreeQueueMode::Dynamic>(t, t, /*per_producer=*/250000, /*capacity=*/1024);
}

TEST(LockFreeQueueStress, FixedMpmc) {
    const unsigned t = stress_threads();
    run_mpmc_stress<LockFreeQueueMode::Fixed>(t, t, /*per_producer=*/250000, /*capacity=*/4096);
}

// High contention over a tiny Fixed capacity exercises the full / retry paths.
TEST(LockFreeQueueStress, FixedHighContentionTinyCapacity) {
    const unsigned t = stress_threads();
    run_mpmc_stress<LockFreeQueueMode::Fixed>(t, t, /*per_producer=*/50000, /*capacity=*/4);
}

}  // namespace
