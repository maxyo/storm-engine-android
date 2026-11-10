#include "parallel_for.hpp"

#include <cassert>
#include <cstdio>
#include <numeric>
#include <set>

int main()
{
    for (unsigned helpers : {0u, 1u, 3u, 7u})
    {
        storm::ParallelFor pool(helpers);
        assert(pool.helpers() == helpers);
        // Disjoint writes, like sea blocks: each index owns its slot.
        for (size_t count : {size_t(0), size_t(1), size_t(2), size_t(3), size_t(17), size_t(1000)})
        {
            for (int repeat = 0; repeat < 200; ++repeat)
            {
                std::vector<int> hits(count, 0);
                std::atomic<size_t> total{0};
                pool.run(count, [&](size_t i) {
                    ++hits[i];
                    total.fetch_add(i + 1, std::memory_order_relaxed);
                });
                for (int hit : hits)
                    assert(hit == 1);
                assert(total.load() == count * (count + 1) / 2);
            }
        }
        // Work really is spread across threads when helpers exist and the job is long enough.
        std::mutex mutex;
        std::set<std::thread::id> threads;
        pool.run(64, [&](size_t) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            std::lock_guard lock(mutex);
            threads.insert(std::this_thread::get_id());
        });
        assert(threads.size() >= 1 && threads.size() <= helpers + 1);
        if (helpers > 0)
            assert(threads.size() > 1);
    }
    // Construct/destroy without ever running a job, and right after one.
    for (int i = 0; i < 100; ++i)
    {
        storm::ParallelFor idle(2);
        if (i % 2)
            idle.run(5, [](size_t) {});
    }
    std::printf("parallel_for: exactly-once/reuse/shutdown PASS\n");
}
