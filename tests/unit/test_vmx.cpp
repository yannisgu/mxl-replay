#include <doctest/doctest.h>

#include "config/config.hpp"
#include "media/jpeg.hpp"
#include "media/picture.hpp"
#include "media/v210.hpp"
#include "media/vmx.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace replay;

namespace
{
// A smooth picture with some texture: gradients, a soft circle and a little noise, 10-bit.
Frame10 testPicture(int width, int height)
{
    Frame10 frame;
    frame.allocate(width, height);
    std::uint32_t seed = 7;
    auto noise = [&] { return static_cast<int>(((seed = seed * 1103515245u + 12345u) >> 16) % 9) - 4; };
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            double const dx = x - width / 2.0;
            double const dy = y - height / 2.0;
            bool const inside = dx * dx + dy * dy < (height / 3.0) * (height / 3.0);
            int const v = 64 + (x * 700) / width + (inside ? 120 : 0) + noise();
            frame.y[static_cast<std::size_t>(y * width + x)] = static_cast<std::uint16_t>(std::clamp(v, 64, 940));
        }
        for (int x = 0; x < width / 2; ++x)
        {
            frame.cb[static_cast<std::size_t>(y * (width / 2) + x)] = static_cast<std::uint16_t>(256 + (y * 500) / height);
            frame.cr[static_cast<std::size_t>(y * (width / 2) + x)] = static_cast<std::uint16_t>(768 - (x * 1000) / width);
        }
    }
    return frame;
}

double psnrY(Frame10 const& a, Frame10 const& b)
{
    double sum = 0;
    for (std::size_t i = 0; i < a.y.size(); ++i)
    {
        double const d = static_cast<double>(a.y[i]) - static_cast<double>(b.y[i]);
        sum += d * d;
    }
    double const mse = sum / static_cast<double>(a.y.size());
    return mse <= 0 ? 99.0 : 10.0 * std::log10(1023.0 * 1023.0 / mse);
}

std::map<std::string, std::string> env(std::filesystem::path const& dir)
{
    return {{"REPLAY_FORMAT", "64x32p50"}, {"REPLAY_INPUTS", "1"}, {"REPLAY_CHANNELS", "1"}, {"REPLAY_BUFFER_HOURS", "0.01"},
        {"REPLAY_STORAGE_DIR", (dir / "media").string()}, {"REPLAY_STATE_DIR", (dir / "state").string()}, {"REPLAY_STORAGE_MIN_MBPS", "0"},
        {"NMOS_HOST_ADDRESS", "10.4.4.4"}, {"HOST_ID", "ci"}};
}
} // namespace

TEST_CASE("REPLAY_CODEC and the VMX settings")
{
    auto const dir = std::filesystem::temp_directory_path() / "mxl-replay-vmx-config";
    std::filesystem::remove_all(dir);
    auto const statePath = (dir / "state" / "config.json").string();
    auto e = env(dir);
    auto loaded = loadStartupConfig(e, {}, statePath);
    CHECK(loaded.config.codec == PictureCodec::Jpeg);
    CHECK(loaded.config.vmx.profile == 99);
    CHECK(loaded.config.vmx.quality == 0);
    CHECK(loaded.config.vmx.threads == 1);
    e["REPLAY_CODEC"] = "VMX";
    e["REPLAY_VMX_PROFILE"] = "omt_sq";
    e["REPLAY_VMX_QUALITY"] = "90";
    e["REPLAY_VMX_THREADS"] = "2";
    loaded = loadStartupConfig(e, {}, statePath);
    CHECK(loaded.config.codec == PictureCodec::Vmx);
    CHECK(loaded.config.vmx.profile == 166);
    CHECK(loaded.config.vmx.quality == 90);
    CHECK(loaded.config.vmx.threads == 2);
    CHECK(exportKeyValue(loaded.config).find("REPLAY_CODEC=vmx\n") != std::string::npos);
    CHECK(exportKeyValue(loaded.config).find("REPLAY_VMX_PROFILE=omt_sq\n") != std::string::npos);
    for (auto const& [key, value] : std::map<std::string, std::string>{
             {"REPLAY_CODEC", "png"}, {"REPLAY_VMX_PROFILE", "best"}, {"REPLAY_VMX_QUALITY", "99"}, {"REPLAY_VMX_THREADS", "0"}})
    {
        auto bad = env(dir);
        bad[key] = value;
        CHECK_THROWS_AS((void)loadStartupConfig(bad, {}, statePath), ConfigError);
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("VMX profile names")
{
    for (auto const* name : {"lq", "sq", "hq", "omt_lq", "omt_sq", "omt_hq"})
    {
        CHECK(vmxProfileName(parseVmxProfile(name)) == name);
    }
    CHECK(parseVmxProfile("HQ") == -1);
}

TEST_CASE("VMX pictures: 10-bit round trip, v210, previews, and a buffer that holds JPEG too")
{
    if (!vmxAvailable())
    {
        MESSAGE("VMX is not available on this CPU: skipped");
        return;
    }
    // 1926 is not a multiple of 6: the last v210 group is partial.
    for (int const width : {1920, 1926, 64})
    {
        int const height = width == 64 ? 32 : 1080;
        auto const picture = testPicture(width, height);
        std::vector<std::uint8_t> packed(v210Size(width, height));
        packV210(picture, packed.data(), 0);

        PictureSettings settings;
        settings.codec = PictureCodec::Vmx;
        settings.vmx.quality = 98;
        auto const fromV210 = encodePictureV210(packed.data(), width, height, 0, settings);
        auto const fromFrame = encodePicture422(picture, settings);
        REQUIRE_FALSE(fromV210.empty());
        CHECK(isVmxPicture(fromV210.data(), fromV210.size()));
        Frame10 decoded;
        REQUIRE(decodePicture422(fromV210.data(), fromV210.size(), decoded));
        // The same samples reach the codec either way. Not the same bytes: libvmx leaves bits of the
        // frame before in a slice's padding. And only at a width that is a multiple of 16: past the
        // edge of a narrower picture the last chroma block reads plane padding libvmx never clears,
        // so its edge pixels vary a little (1926 here; house rasters are multiples of 16).
        Frame10 decodedFromFrame;
        REQUIRE(decodePicture422(fromFrame.data(), fromFrame.size(), decodedFromFrame));
        if (width % 16 == 0)
        {
            CHECK(decodedFromFrame.y == decoded.y);
            CHECK(decodedFromFrame.cb == decoded.cb);
            CHECK(decodedFromFrame.cr == decoded.cr);
        }
        else
        {
            CHECK(psnrY(decodedFromFrame, decoded) > 50.0);
        }
        CHECK(decoded.width == width);
        CHECK(decoded.height == height);
        CHECK(psnrY(picture, decoded) > 45.0);

        // Straight into v210: the same picture as the 16-bit decode, packed.
        std::vector<std::uint8_t> straight(v210Size(width, height), 0xAA);
        REQUIRE(decodePictureToV210(fromV210.data(), fromV210.size(), width, height, 0, straight.data()));
        std::vector<std::uint8_t> viaFrame(v210Size(width, height));
        packV210(decoded, viaFrame.data(), 0);
        CHECK(straight == viaFrame);
        CHECK_FALSE(decodePictureToV210(fromV210.data(), fromV210.size(), width + 6, height, 0, straight.data()));

        Frame10 small;
        REQUIRE(decodePicturePreview(fromV210.data(), fromV210.size(), 16, small));
        CHECK(small.width >= 16);
        CHECK(small.width <= width);
        if (width >= 1920)
        {
            REQUIRE(decodePicturePreview(fromV210.data(), fromV210.size(), 384, small));
            CHECK(small.width >= 384);
            CHECK(small.width <= 960);
            // The middle of the preview is the circle, brighter than the left edge.
            CHECK(small.y[static_cast<std::size_t>(small.height / 2 * small.width + small.width / 2)] >
                  small.y[static_cast<std::size_t>(small.height / 2 * small.width + 2)]);
        }
    }

    // JPEG and VMX side by side: each decodes with its own codec.
    auto const picture = testPicture(64, 32);
    auto const jpeg = encodeJpeg422(picture, 92);
    CHECK_FALSE(isVmxPicture(jpeg.data(), jpeg.size()));
    Frame10 decoded;
    CHECK(decodePicture422(jpeg.data(), jpeg.size(), decoded));
    PictureSettings settings;
    settings.codec = PictureCodec::Vmx;
    auto const vmx = encodePicture422(picture, settings);
    CHECK(decodePicture422(vmx.data(), vmx.size(), decoded));
    CHECK(pictureBytesPerFrameEstimate(1920, 1080, settings) > 100000);

    // Damaged pictures fail without crashing.
    std::vector<std::uint8_t> cut(vmx.begin(), vmx.begin() + static_cast<std::ptrdiff_t>(vmx.size() / 2));
    (void)decodePicture422(cut.data(), cut.size(), decoded);
    std::vector<std::uint8_t> header(vmx.begin(), vmx.begin() + 13);
    CHECK_FALSE(decodePicture422(header.data(), header.size(), decoded));
    auto odd = vmx;
    odd[4] = 63; // width 63: odd
    CHECK_FALSE(decodePicture422(odd.data(), odd.size(), decoded));
}
