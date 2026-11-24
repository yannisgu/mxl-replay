// The stored picture codecs on real pictures: encode and decode time on one thread, size, PSNR.
//
//   mxl-replay-codec-bench <pictures.v210> <width> <height> [label]
//
// The file holds raw v210 pictures of width × height (ffmpeg -pix_fmt v210 -f rawvideo). Each
// codec encodes every picture from v210 and decodes it back to v210 the way the replay records
// and plays (encodePictureV210, decodePictureToV210), then decodes a UI preview. PSNR is on the
// 10-bit samples against the source picture, Y and Cb/Cr apart. The output is a Markdown table.

#include "media/jpeg.hpp"
#include "media/picture.hpp"
#include "media/v210.hpp"
#include "media/vmx.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace replay;

namespace
{
using Clock = std::chrono::steady_clock;

double seconds(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double>(b - a).count();
}

struct Psnr
{
    double y = 0;
    double c = 0;
};

// Squared errors of one decoded picture against its source, Y and chroma.
void addErrors(Frame10 const& a, Frame10 const& b, double& sy, double& sc)
{
    for (std::size_t i = 0; i < a.y.size(); ++i)
    {
        double const d = static_cast<double>(a.y[i]) - static_cast<double>(b.y[i]);
        sy += d * d;
    }
    for (std::size_t i = 0; i < a.cb.size(); ++i)
    {
        double const d1 = static_cast<double>(a.cb[i]) - static_cast<double>(b.cb[i]);
        double const d2 = static_cast<double>(a.cr[i]) - static_cast<double>(b.cr[i]);
        sc += d1 * d1 + d2 * d2;
    }
}

double psnr(double sum, double count)
{
    double const mse = sum / std::max(1.0, count);
    return mse <= 0 ? 99.0 : 10.0 * std::log10(1023.0 * 1023.0 / mse);
}

struct Case
{
    std::string name;
    PictureSettings settings;
};
} // namespace

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: %s <pictures.v210> <width> <height> [label]\n", argv[0]);
        return 2;
    }
    int const width = std::atoi(argv[2]);
    int const height = std::atoi(argv[3]);
    std::string const label = argc > 4 ? argv[4] : argv[1];
    auto const frameBytes = v210Size(width, height);
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t const count = file.size() / frameBytes;
    if (count == 0)
    {
        std::fprintf(stderr, "no %dx%d v210 picture in %s\n", width, height, argv[1]);
        return 2;
    }

    std::vector<Case> cases;
    for (int q : {92, 80})
    {
        PictureSettings s;
        s.codec = PictureCodec::Jpeg;
        s.jpegQuality = q;
        cases.push_back({"JPEG q" + std::to_string(q) + " (libjpeg-turbo, 8-bit)", s});
    }
    if (vmxAvailable())
    {
        for (auto const* profile : {"hq", "sq", "omt_hq", "omt_sq"})
        {
            PictureSettings s;
            s.codec = PictureCodec::Vmx;
            s.vmx.profile = parseVmxProfile(profile);
            cases.push_back({std::string("VMX ") + profile + " (10-bit)", s});
        }
        for (int q : {98, 95, 90})
        {
            PictureSettings s;
            s.codec = PictureCodec::Vmx;
            s.vmx.quality = q;
            cases.push_back({"VMX fixed q" + std::to_string(q) + " (10-bit)", s});
        }
    }
    else
    {
        std::fprintf(stderr, "VMX is not available on this CPU or build\n");
    }

    std::vector<Frame10> sources(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        sources[i].allocate(width, height);
        unpackV210(file.data() + i * frameBytes, 0, sources[i]);
    }

    std::printf("%s: %zu pictures %dx%d, one thread per codec call\n\n", label.c_str(), count, width, height);
    std::printf("| Codec | Encode ms | Decode ms | Preview ms | KB/picture | GB/h at 50p | PSNR Y dB | PSNR C dB |\n");
    std::printf("| --- | --- | --- | --- | --- | --- | --- | --- |\n");
    std::vector<std::uint8_t> v210(frameBytes);
    for (auto const& c : cases)
    {
        // Two passes over the pictures: the first warms the codec (and VMX's rate control), the second is timed.
        std::vector<std::vector<std::uint8_t>> encoded(count);
        for (int pass = 0; pass < 2; ++pass)
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                encoded[i] = encodePictureV210(file.data() + i * frameBytes, width, height, 0, c.settings);
            }
        }
        auto t0 = Clock::now();
        std::size_t bytes = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            encoded[i] = encodePictureV210(file.data() + i * frameBytes, width, height, 0, c.settings);
            bytes += encoded[i].size();
        }
        double const encodeS = seconds(t0, Clock::now());
        bool ok = true;
        t0 = Clock::now();
        for (std::size_t i = 0; i < count; ++i)
        {
            ok = decodePictureToV210(encoded[i].data(), encoded[i].size(), width, height, 0, v210.data()) && ok;
        }
        double const decodeS = seconds(t0, Clock::now());
        Frame10 small;
        t0 = Clock::now();
        for (std::size_t i = 0; i < count; ++i)
        {
            ok = decodePicturePreview(encoded[i].data(), encoded[i].size(), 384, small) && ok;
        }
        double const previewS = seconds(t0, Clock::now());
        double sy = 0;
        double sc = 0;
        Frame10 decoded;
        decoded.allocate(width, height);
        for (std::size_t i = 0; i < count; ++i)
        {
            ok = decodePictureToV210(encoded[i].data(), encoded[i].size(), width, height, 0, v210.data()) && ok;
            unpackV210(v210.data(), 0, decoded);
            addErrors(sources[i], decoded, sy, sc);
        }
        double const n = static_cast<double>(count);
        double const kb = static_cast<double>(bytes) / n / 1024.0;
        std::printf("| %s | %.2f | %.2f | %.2f | %.0f | %.0f | %.2f | %.2f |%s\n", c.name.c_str(), encodeS / n * 1000.0, decodeS / n * 1000.0,
            previewS / n * 1000.0, kb, kb * 1024.0 * 50.0 * 3600.0 / 1e9, psnr(sy, n * width * height), psnr(sc, n * width * height),
            ok ? "" : " (decode failed)");
    }
    return 0;
}
