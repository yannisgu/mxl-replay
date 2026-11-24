#include "media/vmx.hpp"

#include "media/v210.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

#if defined(REPLAY_WITH_VMX)
#include <vmxcodec.h>
#endif

namespace replay
{
namespace
{
constexpr std::uint8_t kMagic[4] = {'V', 'M', 'X', '1'};
constexpr std::size_t kHeader = 12;

std::uint32_t readLe32(std::uint8_t const* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

void writeLe32(std::uint8_t* p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
}

bool headerSize(std::uint8_t const* data, std::size_t size, int& width, int& height)
{
    if (!isVmxPicture(data, size))
    {
        return false;
    }
    auto const w = readLe32(data + 4);
    auto const h = readLe32(data + 8);
    if (w < 16 || h < 16 || w > 16384 || h > 16384 || (w & 1) != 0)
    {
        return false;
    }
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

// One v210 line → the P216 Y line and the interleaved Cb/Cr line (10 bits in the top of 16).
void v210LineToP216(std::uint8_t const* line, int width, std::uint16_t* y, std::uint16_t* uv)
{
    int const full = width / 6;
    for (int group = 0; group < full; ++group)
    {
        std::uint32_t w[4];
        std::memcpy(w, line + static_cast<std::size_t>(group) * 16u, sizeof(w));
        auto* yo = y + group * 6;
        auto* co = uv + group * 6;
        yo[0] = static_cast<std::uint16_t>(((w[0] >> 10) & 0x3ffu) << 6);
        yo[1] = static_cast<std::uint16_t>((w[1] & 0x3ffu) << 6);
        yo[2] = static_cast<std::uint16_t>(((w[1] >> 20) & 0x3ffu) << 6);
        yo[3] = static_cast<std::uint16_t>(((w[2] >> 10) & 0x3ffu) << 6);
        yo[4] = static_cast<std::uint16_t>((w[3] & 0x3ffu) << 6);
        yo[5] = static_cast<std::uint16_t>(((w[3] >> 20) & 0x3ffu) << 6);
        co[0] = static_cast<std::uint16_t>((w[0] & 0x3ffu) << 6);         // Cb0
        co[1] = static_cast<std::uint16_t>(((w[0] >> 20) & 0x3ffu) << 6); // Cr0
        co[2] = static_cast<std::uint16_t>(((w[1] >> 10) & 0x3ffu) << 6); // Cb1
        co[3] = static_cast<std::uint16_t>((w[2] & 0x3ffu) << 6);         // Cr1
        co[4] = static_cast<std::uint16_t>(((w[2] >> 20) & 0x3ffu) << 6); // Cb2
        co[5] = static_cast<std::uint16_t>(((w[3] >> 10) & 0x3ffu) << 6); // Cr2
    }
    if (full * 6 < width)
    {
        // The last, partial group (a width that is not a multiple of 6).
        std::uint32_t w[4];
        std::memcpy(w, line + static_cast<std::size_t>(full) * 16u, sizeof(w));
        std::uint16_t const ys[6] = {static_cast<std::uint16_t>((w[0] >> 10) & 0x3ffu), static_cast<std::uint16_t>(w[1] & 0x3ffu),
            static_cast<std::uint16_t>((w[1] >> 20) & 0x3ffu), static_cast<std::uint16_t>((w[2] >> 10) & 0x3ffu), static_cast<std::uint16_t>(w[3] & 0x3ffu),
            static_cast<std::uint16_t>((w[3] >> 20) & 0x3ffu)};
        std::uint16_t const cs[6] = {static_cast<std::uint16_t>(w[0] & 0x3ffu), static_cast<std::uint16_t>((w[0] >> 20) & 0x3ffu),
            static_cast<std::uint16_t>((w[1] >> 10) & 0x3ffu), static_cast<std::uint16_t>(w[2] & 0x3ffu), static_cast<std::uint16_t>((w[2] >> 20) & 0x3ffu),
            static_cast<std::uint16_t>((w[3] >> 10) & 0x3ffu)};
        for (int i = 0; full * 6 + i < width; ++i)
        {
            y[full * 6 + i] = static_cast<std::uint16_t>(ys[i] << 6);
            uv[full * 6 + i] = static_cast<std::uint16_t>(cs[i] << 6);
        }
    }
}

std::uint32_t s10(std::uint16_t v)
{
    return static_cast<std::uint32_t>(v >> 6);
}

void p216LineToV210(std::uint16_t const* y, std::uint16_t const* uv, int width, std::uint8_t* line)
{
    // Whole groups straight from the planes (a variable-length copy per group cost more than the
    // decode itself).
    int const full = width / 6;
    for (int group = 0; group < full; ++group)
    {
        auto const* ys = y + group * 6;
        auto const* cs = uv + group * 6;
        std::uint32_t w[4];
        w[0] = s10(cs[0]) | (s10(ys[0]) << 10) | (s10(cs[1]) << 20);
        w[1] = s10(ys[1]) | (s10(cs[2]) << 10) | (s10(ys[2]) << 20);
        w[2] = s10(cs[3]) | (s10(ys[3]) << 10) | (s10(cs[4]) << 20);
        w[3] = s10(ys[4]) | (s10(cs[5]) << 10) | (s10(ys[5]) << 20);
        std::memcpy(line + static_cast<std::size_t>(group) * 16u, w, sizeof(w));
    }
    int const groups = (width + 5) / 6;
    for (int group = full; group < groups; ++group)
    {
        std::uint16_t ys[6] = {};
        std::uint16_t cs[6] = {};
        int const n = std::min(6, width - group * 6);
        std::memcpy(ys, y + group * 6, static_cast<std::size_t>(n) * 2u);
        std::memcpy(cs, uv + group * 6, static_cast<std::size_t>(n) * 2u);
        std::uint32_t w[4];
        w[0] = s10(cs[0]) | (s10(ys[0]) << 10) | (s10(cs[1]) << 20);
        w[1] = s10(ys[1]) | (s10(cs[2]) << 10) | (s10(ys[2]) << 20);
        w[2] = s10(cs[3]) | (s10(ys[3]) << 10) | (s10(cs[4]) << 20);
        w[3] = s10(ys[4]) | (s10(cs[5]) << 10) | (s10(ys[5]) << 20);
        std::memcpy(line + static_cast<std::size_t>(group) * 16u, w, sizeof(w));
    }
}

#if defined(REPLAY_WITH_VMX)
// One libvmx instance for one raster and profile, with its work buffers, per thread.
struct Codec
{
    VMX_INSTANCE* instance = nullptr;
    int width = 0;
    int height = 0;
    int profile = 0;
    int quality = 0;
    int threads = 0;
    std::vector<std::uint16_t> p216;
    std::vector<std::uint8_t> out;

    Codec() = default;
    Codec(Codec const&) = delete;
    Codec& operator=(Codec const&) = delete;
    ~Codec()
    {
        if (instance != nullptr)
        {
            VMX_Destroy(instance);
        }
    }
};

// The instance for this raster on this thread: an encoder and a decoder each, since an encoder's
// rate control must not see the frames a decode loaded.
Codec* codecFor(bool encoder, int width, int height, VmxSettings const& settings)
{
    thread_local std::unique_ptr<Codec> encoders;
    thread_local std::unique_ptr<Codec> decoders;
    auto& slot = encoder ? encoders : decoders;
    int const profile = encoder ? settings.profile : VMX_PROFILE_HQ;
    int const quality = encoder ? settings.quality : 0;
    int const threads = std::max(1, settings.threads);
    if (slot && slot->width == width && slot->height == height && slot->profile == profile && slot->quality == quality && slot->threads == threads)
    {
        return slot.get();
    }
    slot.reset();
    auto codec = std::make_unique<Codec>();
    codec->instance = VMX_Create(VMX_SIZE{width, height}, static_cast<VMX_PROFILE>(profile), VMX_COLORSPACE_BT709);
    if (codec->instance == nullptr)
    {
        return nullptr;
    }
    VMX_SetThreads(codec->instance, threads);
    if (quality > 0)
    {
        // No rate control: the frame size follows the picture.
        int frameMin = 0;
        int frameMax = 0;
        int minQuality = 0;
        int dcShift = 0;
        VMX_GetEncodingParameters(codec->instance, &frameMin, &frameMax, &minQuality, &dcShift);
        VMX_SetEncodingParameters(codec->instance, 0, 0, std::min(minQuality, quality), dcShift);
        VMX_SetQuality(codec->instance, quality);
    }
    codec->width = width;
    codec->height = height;
    codec->profile = profile;
    codec->quality = quality;
    codec->threads = threads;
    codec->p216.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 2u);
    slot = std::move(codec);
    return slot.get();
}

std::vector<std::uint8_t> save(Codec& codec)
{
    // libvmx asks for room for width × height × 4.
    codec.out.resize(kHeader + static_cast<std::size_t>(codec.width) * static_cast<std::size_t>(codec.height) * 4u);
    int const length = VMX_SaveTo(codec.instance, codec.out.data() + kHeader, static_cast<int>(codec.out.size() - kHeader));
    if (length <= 0)
    {
        return {};
    }
    std::memcpy(codec.out.data(), kMagic, 4);
    writeLe32(codec.out.data() + 4, static_cast<std::uint32_t>(codec.width));
    writeLe32(codec.out.data() + 8, static_cast<std::uint32_t>(codec.height));
    return {codec.out.begin(), codec.out.begin() + static_cast<std::ptrdiff_t>(kHeader) + length};
}

Codec* load(std::uint8_t const* data, std::size_t size, int& width, int& height)
{
    if (!vmxAvailable() || !headerSize(data, size, width, height) || size > static_cast<std::size_t>(INT32_MAX))
    {
        return nullptr;
    }
    auto* codec = codecFor(false, width, height, VmxSettings{});
    if (codec == nullptr ||
        VMX_LoadFrom(codec->instance, const_cast<BYTE*>(data + kHeader), static_cast<int>(size - kHeader)) != VMX_ERR_OK)
    {
        return nullptr;
    }
    return codec;
}
#endif
} // namespace

bool vmxAvailable()
{
#if defined(REPLAY_WITH_VMX)
    static bool const available = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("bmi") && __builtin_cpu_supports("bmi2") &&
                                  __builtin_cpu_supports("sse4.2");
    return available;
#else
    return false;
#endif
}

bool isVmxPicture(std::uint8_t const* data, std::size_t size)
{
    return data != nullptr && size > kHeader && std::memcmp(data, kMagic, 4) == 0;
}

int parseVmxProfile(std::string const& name)
{
    if (name == "lq")
    {
        return 33;
    }
    if (name == "sq")
    {
        return 66;
    }
    if (name == "hq")
    {
        return 99;
    }
    if (name == "omt_lq")
    {
        return 133;
    }
    if (name == "omt_sq")
    {
        return 166;
    }
    if (name == "omt_hq")
    {
        return 199;
    }
    return -1;
}

std::string vmxProfileName(int profile)
{
    switch (profile)
    {
    case 33:
        return "lq";
    case 66:
        return "sq";
    case 133:
        return "omt_lq";
    case 166:
        return "omt_sq";
    case 199:
        return "omt_hq";
    default:
        return "hq";
    }
}

std::vector<std::uint8_t> encodeVmxV210(std::uint8_t const* v210, int width, int height, int rowBytes, VmxSettings const& settings)
{
#if defined(REPLAY_WITH_VMX)
    if (!vmxAvailable() || v210 == nullptr)
    {
        return {};
    }
    auto* codec = codecFor(true, width, height, settings);
    if (codec == nullptr)
    {
        return {};
    }
    if (rowBytes <= 0)
    {
        rowBytes = static_cast<int>(v210RowBytes(width));
    }
    auto* y = codec->p216.data();
    auto* uv = y + static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (int row = 0; row < height; ++row)
    {
        v210LineToP216(v210 + static_cast<std::size_t>(row) * static_cast<std::size_t>(rowBytes), width, y + static_cast<std::size_t>(row) * width,
            uv + static_cast<std::size_t>(row) * width);
    }
    if (VMX_EncodeP216(codec->instance, reinterpret_cast<BYTE*>(codec->p216.data()), width * 2, 0) != VMX_ERR_OK)
    {
        return {};
    }
    return save(*codec);
#else
    return {};
#endif
}

std::vector<std::uint8_t> encodeVmx422(Frame10 const& frame, VmxSettings const& settings)
{
#if defined(REPLAY_WITH_VMX)
    if (!vmxAvailable() || frame.empty())
    {
        return {};
    }
    int const width = frame.width;
    int const height = frame.height;
    auto* codec = codecFor(true, width, height, settings);
    if (codec == nullptr)
    {
        return {};
    }
    int const cw = width / 2;
    auto* y = codec->p216.data();
    auto* uv = y + static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (int row = 0; row < height; ++row)
    {
        auto const* ys = frame.y.data() + static_cast<std::size_t>(row) * width;
        auto const* cbs = frame.cb.data() + static_cast<std::size_t>(row) * cw;
        auto const* crs = frame.cr.data() + static_cast<std::size_t>(row) * cw;
        auto* yo = y + static_cast<std::size_t>(row) * width;
        auto* co = uv + static_cast<std::size_t>(row) * width;
        for (int x = 0; x < width; ++x)
        {
            yo[x] = static_cast<std::uint16_t>((ys[x] & 0x3ffu) << 6);
        }
        for (int x = 0; x < cw; ++x)
        {
            co[2 * x] = static_cast<std::uint16_t>((cbs[x] & 0x3ffu) << 6);
            co[2 * x + 1] = static_cast<std::uint16_t>((crs[x] & 0x3ffu) << 6);
        }
    }
    if (VMX_EncodeP216(codec->instance, reinterpret_cast<BYTE*>(codec->p216.data()), width * 2, 0) != VMX_ERR_OK)
    {
        return {};
    }
    return save(*codec);
#else
    return {};
#endif
}

bool decodeVmx422(std::uint8_t const* data, std::size_t size, Frame10& frame)
{
#if defined(REPLAY_WITH_VMX)
    int width = 0;
    int height = 0;
    auto* codec = load(data, size, width, height);
    if (codec == nullptr || VMX_DecodeP216(codec->instance, reinterpret_cast<BYTE*>(codec->p216.data()), width * 2) != VMX_ERR_OK)
    {
        return false;
    }
    frame.allocate(width, height);
    int const cw = width / 2;
    auto const* y = codec->p216.data();
    auto const* uv = y + static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (int row = 0; row < height; ++row)
    {
        auto const* ys = y + static_cast<std::size_t>(row) * width;
        auto const* cs = uv + static_cast<std::size_t>(row) * width;
        auto* yo = frame.y.data() + static_cast<std::size_t>(row) * width;
        auto* cbo = frame.cb.data() + static_cast<std::size_t>(row) * cw;
        auto* cro = frame.cr.data() + static_cast<std::size_t>(row) * cw;
        for (int x = 0; x < width; ++x)
        {
            yo[x] = static_cast<std::uint16_t>(ys[x] >> 6);
        }
        for (int x = 0; x < cw; ++x)
        {
            cbo[x] = static_cast<std::uint16_t>(cs[2 * x] >> 6);
            cro[x] = static_cast<std::uint16_t>(cs[2 * x + 1] >> 6);
        }
    }
    return true;
#else
    return false;
#endif
}

bool decodeVmxToV210(std::uint8_t const* data, std::size_t size, int width, int height, int rowBytes, std::uint8_t* v210)
{
#if defined(REPLAY_WITH_VMX)
    int w = 0;
    int h = 0;
    if (v210 == nullptr || !headerSize(data, size, w, h) || w != width || h != height)
    {
        return false;
    }
    auto* codec = load(data, size, w, h);
    if (codec == nullptr || VMX_DecodeP216(codec->instance, reinterpret_cast<BYTE*>(codec->p216.data()), width * 2) != VMX_ERR_OK)
    {
        return false;
    }
    if (rowBytes <= 0)
    {
        rowBytes = static_cast<int>(v210RowBytes(width));
    }
    auto const* y = codec->p216.data();
    auto const* uv = y + static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (int row = 0; row < height; ++row)
    {
        p216LineToV210(y + static_cast<std::size_t>(row) * width, uv + static_cast<std::size_t>(row) * width, width,
            v210 + static_cast<std::size_t>(row) * static_cast<std::size_t>(rowBytes));
    }
    return true;
#else
    return false;
#endif
}

bool decodeVmxPreview(std::uint8_t const* data, std::size_t size, int minWidth, Frame10& frame)
{
#if defined(REPLAY_WITH_VMX)
    int width = 0;
    int height = 0;
    auto* codec = load(data, size, width, height);
    if (codec == nullptr)
    {
        return false;
    }
    // UYVY, 8-bit: libvmx's 1/8 picture when it is wide enough, else the full picture.
    int factor = 8;
    while (factor > 1 && width / factor < minWidth)
    {
        factor /= 2;
    }
    bool const preview = factor == 8;
    int const srcW = preview ? width / 8 : width;
    int const srcH = preview ? height / 8 : height;
    std::vector<std::uint8_t> uyvy(static_cast<std::size_t>(srcW) * static_cast<std::size_t>(srcH) * 2u);
    auto const err = preview ? VMX_DecodePreviewUYVY(codec->instance, uyvy.data(), srcW * 2) : VMX_DecodeUYVY(codec->instance, uyvy.data(), srcW * 2);
    if (err != VMX_ERR_OK)
    {
        return false;
    }
    int const step = preview ? 1 : factor;
    int const outW = (srcW / step) & ~1;
    int const outH = srcH / step;
    if (outW < 2 || outH < 1)
    {
        return false;
    }
    frame.allocate(outW, outH);
    int const cw = outW / 2;
    // A box of step × step source pixels per output pixel (chroma: step × step chroma samples).
    for (int row = 0; row < outH; ++row)
    {
        for (int x = 0; x < outW; ++x)
        {
            unsigned sum = 0;
            for (int dy = 0; dy < step; ++dy)
            {
                auto const* line = uyvy.data() + static_cast<std::size_t>(row * step + dy) * static_cast<std::size_t>(srcW) * 2u;
                for (int dx = 0; dx < step; ++dx)
                {
                    sum += line[(x * step + dx) * 2 + 1];
                }
            }
            frame.y[static_cast<std::size_t>(row) * outW + x] = static_cast<std::uint16_t>((sum << 2) / static_cast<unsigned>(step * step));
        }
        for (int x = 0; x < cw; ++x)
        {
            unsigned cb = 0;
            unsigned cr = 0;
            for (int dy = 0; dy < step; ++dy)
            {
                auto const* line = uyvy.data() + static_cast<std::size_t>(row * step + dy) * static_cast<std::size_t>(srcW) * 2u;
                for (int dx = 0; dx < step; ++dx)
                {
                    auto const* pair = line + static_cast<std::size_t>(x * step + dx) * 4u;
                    cb += pair[0];
                    cr += pair[2];
                }
            }
            frame.cb[static_cast<std::size_t>(row) * cw + x] = static_cast<std::uint16_t>((cb << 2) / static_cast<unsigned>(step * step));
            frame.cr[static_cast<std::size_t>(row) * cw + x] = static_cast<std::uint16_t>((cr << 2) / static_cast<unsigned>(step * step));
        }
    }
    return true;
#else
    return false;
#endif
}

double vmxBytesPerFrameEstimate(int width, int height, VmxSettings const& settings)
{
#if defined(REPLAY_WITH_VMX)
    if (vmxAvailable())
    {
        int const profile = settings.quality > 0 ? VMX_PROFILE_HQ : settings.profile;
        if (auto* instance = VMX_Create(VMX_SIZE{width, height}, static_cast<VMX_PROFILE>(profile), VMX_COLORSPACE_BT709))
        {
            int frameMin = 0;
            int frameMax = 0;
            int minQuality = 0;
            int dcShift = 0;
            VMX_GetEncodingParameters(instance, &frameMin, &frameMax, &minQuality, &dcShift);
            VMX_Destroy(instance);
            if (frameMax > 0)
            {
                return static_cast<double>(frameMax);
            }
        }
    }
#endif
    // HQ at 1080: about 0.29 bytes per pixel.
    return static_cast<double>(width) * static_cast<double>(height) * 0.29;
}
} // namespace replay
