// What one encode of a stored picture codec costs when N threads encode at once, each paced at 50
// pictures per second (as the cameras record) or running flat out.
//
//   mxl-replay-encode-scale <pictures.v210> <width> <height> <threads> <seconds> <paced 0|1>
//
// For each codec (JPEG q92, VMX hq, VMX q95) it prints the thread CPU time and the wall time per
// encode in ms, the encodes per second in total, and the cores used.

#include "media/picture.hpp"
#include "media/v210.hpp"
#include "media/vmx.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using namespace replay;

namespace
{
double threadCpu()
{
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 7)
    {
        std::fprintf(stderr, "usage: %s <pictures.v210> <width> <height> <threads> <seconds> <paced>\n", argv[0]);
        return 2;
    }
    int const width = std::atoi(argv[2]);
    int const height = std::atoi(argv[3]);
    int const threads = std::atoi(argv[4]);
    double const seconds = std::atof(argv[5]);
    bool const paced = std::atoi(argv[6]) != 0;
    auto const bytes = v210Size(width, height);
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t const count = file.size() / bytes;

    std::vector<std::pair<std::string, PictureSettings>> codecs;
    codecs.emplace_back("JPEG q92", PictureSettings{});
    if (vmxAvailable())
    {
        PictureSettings hq;
        hq.codec = PictureCodec::Vmx;
        codecs.emplace_back("VMX hq", hq);
        PictureSettings q95 = hq;
        q95.vmx.quality = 95;
        codecs.emplace_back("VMX q95", q95);
    }
    for (auto const& [name, settings] : codecs)
    {
        std::vector<double> cpu(threads), wall(threads);
        std::vector<long> done(threads);
        std::vector<std::thread> pool;
        auto const start = std::chrono::steady_clock::now();
        for (int t = 0; t < threads; ++t)
        {
            pool.emplace_back([&, t] {
                auto next = start;
                std::size_t i = static_cast<std::size_t>(t) * count / threads;
                // One warm-up encode (thread-local codec instances, rate control).
                encodePictureV210(file.data() + (i % count) * bytes, width, height, 0, settings);
                while (std::chrono::steady_clock::now() - start < std::chrono::duration<double>(seconds))
                {
                    if (paced)
                    {
                        next += std::chrono::milliseconds(20);
                        std::this_thread::sleep_until(next);
                    }
                    double const c0 = threadCpu();
                    auto const w0 = std::chrono::steady_clock::now();
                    auto out = encodePictureV210(file.data() + (i++ % count) * bytes, width, height, 0, settings);
                    wall[t] += std::chrono::duration<double>(std::chrono::steady_clock::now() - w0).count();
                    cpu[t] += threadCpu() - c0;
                    ++done[t];
                }
            });
        }
        for (auto& th : pool)
        {
            th.join();
        }
        double c = 0, w = 0;
        long n = 0;
        for (int t = 0; t < threads; ++t)
        {
            c += cpu[t];
            w += wall[t];
            n += done[t];
        }
        std::printf("| %s | %d | %s | %.2f | %.2f | %.0f | %.2f |\n", name.c_str(), threads, paced ? "50/s" : "flat out", c / n * 1e3, w / n * 1e3,
            n / seconds, c / seconds);
    }
    return 0;
}
