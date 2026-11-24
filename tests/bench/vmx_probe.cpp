// libvmx on its own, without the replay's conversions: encode and decode time of one 1080p picture
// in 8-bit UYVY and 10-bit P216, with 1 and more libvmx threads.
//
//   vmx_probe <pictures.v210> <width> <height> [pictures]
//
// Built twice by tests/bench/lab.sh: with the replay's compiler (GCC) and with clang like upstream.

#include <vmxcodec.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

std::uint16_t s10(std::uint32_t w, int shift)
{
    return static_cast<std::uint16_t>((w >> shift) & 0x3ffu);
}

// v210 → UYVY 8-bit and P216 (Y plane, then interleaved Cb/Cr), widths that are multiples of 6.
void convert(std::uint8_t const* v210, int width, int height, std::uint8_t* uyvy, std::uint16_t* p216)
{
    std::size_t const rowBytes = static_cast<std::size_t>((width + 5) / 6) * 16u;
    std::uint16_t* uv = p216 + static_cast<std::size_t>(width) * height;
    for (int row = 0; row < height; ++row)
    {
        auto const* line = v210 + row * rowBytes;
        for (int g = 0; g < width / 6; ++g)
        {
            std::uint32_t w[4];
            std::memcpy(w, line + g * 16, 16);
            std::uint16_t const y[6] = {s10(w[0], 10), s10(w[1], 0), s10(w[1], 20), s10(w[2], 10), s10(w[3], 0), s10(w[3], 20)};
            std::uint16_t const c[6] = {s10(w[0], 0), s10(w[0], 20), s10(w[1], 10), s10(w[2], 0), s10(w[2], 20), s10(w[3], 10)};
            for (int i = 0; i < 6; ++i)
            {
                std::size_t const x = static_cast<std::size_t>(g) * 6 + i;
                p216[row * static_cast<std::size_t>(width) + x] = static_cast<std::uint16_t>(y[i] << 6);
                uv[row * static_cast<std::size_t>(width) + x] = static_cast<std::uint16_t>(c[i] << 6);
                auto* o = uyvy + (row * static_cast<std::size_t>(width) + x) * 2;
                o[0] = static_cast<std::uint8_t>(c[i] >> 2);
                o[1] = static_cast<std::uint8_t>(y[i] >> 2);
            }
        }
    }
}

double ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: %s <pictures.v210> <width> <height> [pictures]\n", argv[0]);
        return 2;
    }
    int const width = std::atoi(argv[2]);
    int const height = std::atoi(argv[3]);
    std::size_t const frameBytes = static_cast<std::size_t>((width + 5) / 6) * 16u * height;
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t count = file.size() / frameBytes;
    if (argc > 4)
    {
        count = std::min<std::size_t>(count, static_cast<std::size_t>(std::atoi(argv[4])));
    }
    std::size_t const pixels = static_cast<std::size_t>(width) * height;
    std::vector<std::vector<std::uint8_t>> uyvy(count, std::vector<std::uint8_t>(pixels * 2));
    std::vector<std::vector<std::uint16_t>> p216(count, std::vector<std::uint16_t>(pixels * 2));
    for (std::size_t i = 0; i < count; ++i)
    {
        convert(file.data() + i * frameBytes, width, height, uyvy[i].data(), p216[i].data());
    }
    std::vector<std::uint8_t> out(pixels * 4);
    std::vector<std::uint8_t> back(pixels * 4);
    std::printf("| Format | Profile | libvmx threads | Encode ms | Decode ms | KB |\n| --- | --- | --- | --- | --- | --- |\n");
    unsigned const cores = std::thread::hardware_concurrency();
    for (bool const tenBit : {false, true})
    {
        for (int const profile : {VMX_PROFILE_HQ, VMX_PROFILE_SQ})
        {
            for (int const threads : {1, 2, 4})
            {
                if (static_cast<unsigned>(threads) > cores)
                {
                    continue;
                }
                auto* vmx = VMX_Create(VMX_SIZE{width, height}, static_cast<VMX_PROFILE>(profile), VMX_COLORSPACE_BT709);
                VMX_SetThreads(vmx, threads);
                double encode = 0;
                double decode = 0;
                std::size_t bytes = 0;
                for (int pass = 0; pass < 2; ++pass)
                {
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        auto t0 = Clock::now();
                        if (tenBit)
                        {
                            VMX_EncodeP216(vmx, reinterpret_cast<BYTE*>(p216[i].data()), width * 2, 0);
                        }
                        else
                        {
                            VMX_EncodeUYVY(vmx, uyvy[i].data(), width * 2, 0);
                        }
                        int const n = VMX_SaveTo(vmx, out.data(), static_cast<int>(out.size()));
                        auto t1 = Clock::now();
                        VMX_LoadFrom(vmx, out.data(), n);
                        if (tenBit)
                        {
                            VMX_DecodeP216(vmx, back.data(), width * 2);
                        }
                        else
                        {
                            VMX_DecodeUYVY(vmx, back.data(), width * 2);
                        }
                        auto t2 = Clock::now();
                        if (pass == 1)
                        {
                            encode += ms(t0, t1);
                            decode += ms(t1, t2);
                            bytes += static_cast<std::size_t>(n);
                        }
                    }
                }
                VMX_Destroy(vmx);
                std::printf("| %s | %s | %d | %.2f | %.2f | %.0f |\n", tenBit ? "P216 10-bit" : "UYVY 8-bit", profile == VMX_PROFILE_HQ ? "HQ" : "SQ", threads,
                    encode / count, decode / count, bytes / 1024.0 / count);
            }
        }
    }
    // As the replay does it: one encoder and one decoder instance, the encoder's rate control on.
    for (int const profile : {VMX_PROFILE_HQ, VMX_PROFILE_SQ})
    {
        auto* enc = VMX_Create(VMX_SIZE{width, height}, static_cast<VMX_PROFILE>(profile), VMX_COLORSPACE_BT709);
        auto* dec = VMX_Create(VMX_SIZE{width, height}, VMX_PROFILE_HQ, VMX_COLORSPACE_BT709);
        VMX_SetThreads(enc, 1);
        VMX_SetThreads(dec, 1);
        double encode = 0;
        double decode = 0;
        std::size_t bytes = 0;
        for (int pass = 0; pass < 2; ++pass)
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                auto t0 = Clock::now();
                VMX_EncodeP216(enc, reinterpret_cast<BYTE*>(p216[i].data()), width * 2, 0);
                int const n = VMX_SaveTo(enc, out.data(), static_cast<int>(out.size()));
                auto t1 = Clock::now();
                VMX_LoadFrom(dec, out.data(), n);
                VMX_DecodeP216(dec, back.data(), width * 2);
                auto t2 = Clock::now();
                if (pass == 1)
                {
                    encode += ms(t0, t1);
                    decode += ms(t1, t2);
                    bytes += static_cast<std::size_t>(n);
                }
            }
        }
        std::printf("| P216 10-bit, encoder and decoder apart | %s | 1 | %.2f | %.2f | %.0f |\n", profile == VMX_PROFILE_HQ ? "HQ" : "SQ", encode / count,
            decode / count, bytes / 1024.0 / count);
        VMX_Destroy(enc);
        VMX_Destroy(dec);
    }
    return 0;
}
