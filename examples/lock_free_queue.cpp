// A small producer/consumer tour of comms::LockFreeQueue: several producer
// threads push a disjoint range of tagged values into a shared queue while
// several consumer threads drain it, and we check that every value came out
// exactly once. Shown for both the Dynamic (unbounded) and Fixed (bounded)
// modes. Concurrency-only, JSON-free, so it builds in the base config.

#include <commons/lock_free_queue.hpp>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {

// Run `producers` producers and `consumers` consumers over one queue, each
// producer emitting `per_producer` unique values; returns true if every value
// was consumed exactly once.
template <comms::LockFreeQueueMode Mode>
bool run(const unsigned producers,
         const unsigned consumers,
         const std::uint32_t per_producer,
         typename comms::LockFreeQueue<std::uint64_t, Mode>::size_type capacity) {
    comms::LockFreeQueue<std::uint64_t, Mode> queue(capacity);

    const std::uint64_t total = static_cast<std::uint64_t>(producers) * per_producer;
    std::vector<std::atomic<int>> seen(total);
    std::atomic<std::uint64_t> consumed{0};

    std::vector<std::thread> threads;
    threads.reserve(producers + consumers);

    for (unsigned c = 0; c < consumers; ++c) {
        threads.emplace_back([&] {
            while (consumed.load(std::memory_order_acquire) < total) {
                if (std::uint64_t value = 0; queue.pop(value)) {
                    seen[value].fetch_add(1, std::memory_order_relaxed);
                    consumed.fetch_add(1, std::memory_order_acq_rel);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    for (unsigned p = 0; p < producers; ++p) {
        threads.emplace_back([&, p] {
            for (std::uint32_t i = 0; i < per_producer; ++i) {
                const std::uint64_t value = static_cast<std::uint64_t>(p) * per_producer + i;
                while (!queue.push(value)) {
                    std::this_thread::yield();  // Fixed mode may be momentarily full
                }
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    bool ok = consumed.load() == total;
    for (std::uint64_t v = 0; ok && v < total; ++v) {
        ok = seen[v].load(std::memory_order_relaxed) == 1;
    }
    return ok;
}

}  // namespace

int main() {
    namespace c = comms;

    {
        c::LockFreeQueue<int> q;  // Dynamic by default
        std::cout << "Dynamic queue: capacity()=" << q.capacity()
                  << " (0 = unbounded), is_lock_free=" << std::boolalpha << q.is_lock_free()
                  << '\n';
        for (int i = 0; i < 3; ++i) {
            (void)q.push(i * 10);
        }
        int out = 0;
        std::cout << "  drained:";
        while (q.pop(out)) {
            std::cout << ' ' << out;
        }
        std::cout << '\n';
    }

    {
        c::LockFreeQueue<int, c::LockFreeQueueMode::Fixed> q(2);
        std::cout << "Fixed queue: capacity()=" << q.capacity()
                  << ", is_lock_free=" << q.is_lock_free() << '\n';
        std::cout << "  push 1=" << q.push(1) << " push 2=" << q.push(2)
                  << " push 3 (full)=" << q.push(3) << '\n';
    }

    const bool dyn_ok = run<c::LockFreeQueueMode::Dynamic>(4, 4, 50000, 1024);
    const bool fix_ok = run<c::LockFreeQueueMode::Fixed>(4, 4, 50000, 256);
    std::cout << "MPMC stress — Dynamic ok=" << dyn_ok << ", Fixed ok=" << fix_ok << '\n';

    return (dyn_ok && fix_ok) ? 0 : 1;
}
