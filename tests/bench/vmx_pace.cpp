// Times one encode after a gap: none, a 20 ms busy-wait, or a sleep of 2 to 20 ms. On shared vCPUs a
// thread that slept long enough encodes at about half speed. The replay's camera threads sleep between
// pictures, so this hides a faster codec there.
//
//   vmx-pace <pictures.v210> <width> <height>
//
// One thread. For each gap it prints ms per encode and minor page faults per encode, for the
// replay's encodePictureV210 (JPEG q92, VMX hq) and for libvmx alone (VMX_EncodeP216 and VMX_SaveTo
// on a picture converted beforehand).

#include "media/picture.hpp"
#include "media/v210.hpp"
#include "media/vmx.hpp"

#include <vmxcodec.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <vector>

using namespace replay;
using Clock = std::chrono::steady_clock;

namespace
{
long minflt()
{
    rusage u{};
    getrusage(RUSAGE_THREAD, &u);
    return u.ru_minflt;
}

void gap(std::string const& how, int ms)
{
    if (how == "sleep")
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    else if (how == "spin")
    {
        auto const end = Clock::now() + std::chrono::milliseconds(ms);
        while (Clock::now() < end)
        {
        }
    }
}

// The picture as P216 (Y, then Cb/Cr interleaved, 16-bit, MSB-aligned).
void toP216(std::uint8_t const* v210, int width, int height, std::uint16_t* p216)
{
    Frame10 f;
    f.allocate(width, height);
    unpackV210(v210, 0, f);
    auto* uv = p216 + static_cast<std::size_t>(width) * height;
    for (std::size_t i = 0; i < f.y.size(); ++i)
    {
        p216[i] = static_cast<std::uint16_t>(f.y[i] << 6);
    }
    for (std::size_t i = 0; i < f.cb.size(); ++i)
    {
        uv[2 * i] = static_cast<std::uint16_t>(f.cb[i] << 6);
        uv[2 * i + 1] = static_cast<std::uint16_t>(f.cr[i] << 6);
    }
}
} // namespace

int main(int argc, char** argv)
{
    int const width = std::atoi(argv[2]);
    int const height = std::atoi(argv[3]);
    auto const bytes = v210Size(width, height);
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t const count = std::min<std::size_t>(file.size() / bytes, 50);
    std::vector<std::vector<std::uint16_t>> p216(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        p216[i].resize(static_cast<std::size_t>(width) * height * 2);
        toP216(file.data() + i * bytes, width, height, p216[i].data());
    }
    VMX_INSTANCE* raw = VMX_Create(VMX_SIZE{width, height}, VMX_PROFILE_HQ, VMX_COLORSPACE_BT709);
    VMX_SetThreads(raw, 1);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * height * 4);
    PictureSettings jpeg;
    PictureSettings vmx;
    vmx.codec = PictureCodec::Vmx;

    struct Work
    {
        std::string name;
        std::function<void(std::size_t)> run;
    };
    std::vector<Work> works{
        {"JPEG q92 (replay)", [&](std::size_t i) { encodePictureV210(file.data() + i * bytes, width, height, 0, jpeg); }},
        {"VMX hq (replay)", [&](std::size_t i) { encodePictureV210(file.data() + i * bytes, width, height, 0, vmx); }},
        {"VMX hq, libvmx alone", [&](std::size_t i) {
             VMX_EncodeP216(raw, reinterpret_cast<BYTE*>(p216[i].data()), width * 2, 0);
             VMX_SaveTo(raw, out.data(), static_cast<int>(out.size()));
         }},
    };
    std::printf("| Work | Gap | ms per encode | minor faults per encode |\n| --- | --- | --- | --- |\n");
    std::vector<std::pair<std::string, int>> gaps{{"none", 0}, {"spin", 20}, {"sleep", 2}, {"sleep", 5}, {"sleep", 8}, {"sleep", 10}, {"sleep", 12}, {"sleep", 14}, {"sleep", 16}, {"sleep", 20}};
    for (auto const& work : works)
    {
        for (std::size_t i = 0; i < 10; ++i)
        {
            work.run(i % count);
        }
        for (auto const& [how, ms] : gaps)
        {
            double total = 0;
            long faults = 0;
            int const n = 100;
            for (int k = 0; k < n; ++k)
            {
                gap(how, ms);
                long const f0 = minflt();
                auto const t0 = Clock::now();
                work.run(static_cast<std::size_t>(k) % count);
                total += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
                faults += minflt() - f0;
            }
            std::printf("| %s | %s %d ms | %.2f | %.0f |\n", work.name.c_str(), how.c_str(), ms, total / n, double(faults) / n);
        }
    }
    return 0;
}
