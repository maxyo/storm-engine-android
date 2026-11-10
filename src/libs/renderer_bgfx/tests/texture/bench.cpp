// DXT -> ETC2 transcode throughput on synthetic textures (host and Android arm64).
//   storm_texture_bench [max_threads]
#include "cache.hpp"
#include "synthetic.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace storm::texture;

int main(int argc, char **argv)
{
    const unsigned max_threads = argc > 1 ? unsigned(atoi(argv[1])) : 2;
    struct Case
    {
        DxtFormat format;
        uint32_t size;
    };
    const Case cases[] = {{DxtFormat::Bc1, 1024}, {DxtFormat::Bc3, 1024}, {DxtFormat::Bc1, 2048}, {DxtFormat::Bc3, 2048}};
    printf("format size threads  MP/s-per-thread  MP/s-total  ms-per-texture\n");
    for (const Case &tc : cases)
    {
        Layout layout{tc.format, tc.size, tc.size, 1, 1};
        while ((tc.size >> layout.levels) > 0)
            ++layout.levels;
        const auto payload = test::make_payload(layout, test::Pattern::Mixed, 5);
        const EtcFormat format = choose_etc_format(layout.format, payload.data(), payload.size());
        double pixels = 0;
        for (uint32_t level = 0; level < layout.levels; ++level)
            pixels += double(layout.level_width(level)) * layout.level_height(level);
        for (unsigned threads = 1; threads <= max_threads; threads *= 2)
        {
            const int per_thread = tc.size >= 2048 ? 2 : 4;
            const auto start = std::chrono::steady_clock::now();
            std::vector<std::thread> pool;
            for (unsigned t = 0; t < threads; ++t)
                pool.emplace_back([&] {
                    for (int i = 0; i < per_thread; ++i)
                    {
                        Blob blob = transcode_to_ktx(layout, payload.data(), format);
                        if (!blob.size)
                            abort();
                    }
                });
            for (auto &thread : pool)
                thread.join();
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            const double total = pixels * per_thread * threads / 1e6 / seconds;
            printf("%s %5u %7u  %15.1f  %10.1f  %8.1f\n", tc.format == DxtFormat::Bc1 ? "DXT1" : "DXT5", tc.size,
                   threads, total / threads, total, seconds * 1000 / per_thread);
        }
    }
    return 0;
}
