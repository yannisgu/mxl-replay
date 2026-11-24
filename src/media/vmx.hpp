#pragma once

#include "media/frame.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace replay
{
// VMX, the intra-frame codec of Open Media Transport (third_party/libvmx), as the stored picture
// codec (REPLAY_CODEC=vmx). It codes 10-bit 4:2:2 (from v210 as P216): no 8-bit step like JPEG.
//
// A stored VMX picture is a 12-byte header ("VMX1", width and height as 32-bit little endian)
// and libvmx's frame. Decoders tell it from a JPEG by that header, so one buffer can hold both
// (uploads stay JPEG, a camera buffer recorded before a codec change still plays).
//
// Every thread keeps its own codec instance per raster. Encoding uses the profile's rate control
// (the quality follows the frame sizes of that thread's last frames) unless `quality` is set.
struct VmxSettings
{
    // libvmx's VMX_PROFILE: 33 LQ, 66 SQ, 99 HQ, 133/166/199 the OMT profiles.
    int profile = 99;
    // 0: the profile's rate control. 1–98: a fixed quality, no rate control.
    int quality = 0;
    // libvmx's own worker threads per instance (it splits a frame in slices).
    int threads = 1;
};

// False on a CPU without AVX2 and BMI2, or a build without VMX: nothing may be encoded or decoded.
[[nodiscard]] bool vmxAvailable();
[[nodiscard]] bool isVmxPicture(std::uint8_t const* data, std::size_t size);
// "hq", "sq", "lq", "omt_hq", "omt_sq", "omt_lq" → VMX_PROFILE; -1 when unknown.
[[nodiscard]] int parseVmxProfile(std::string const& name);
[[nodiscard]] std::string vmxProfileName(int profile);

// Empty when VMX is not available or the raster is not codable (width odd or under 16).
std::vector<std::uint8_t> encodeVmxV210(std::uint8_t const* v210, int width, int height, int rowBytes, VmxSettings const& settings);
std::vector<std::uint8_t> encodeVmx422(Frame10 const& frame, VmxSettings const& settings);
bool decodeVmx422(std::uint8_t const* data, std::size_t size, Frame10& frame);
// False when the picture is not width × height.
bool decodeVmxToV210(std::uint8_t const* data, std::size_t size, int width, int height, int rowBytes, std::uint8_t* v210);
// At least `minWidth` wide: libvmx's own 1/8 preview when that is wide enough, else an 8-bit
// decode averaged down by 2, 4 or 8.
bool decodeVmxPreview(std::uint8_t const* data, std::size_t size, int minWidth, Frame10& frame);

// The profile's target size per frame (libvmx sizes its targets per frame, whatever the rate);
// with a fixed quality, the HQ target.
[[nodiscard]] double vmxBytesPerFrameEstimate(int width, int height, VmxSettings const& settings);
} // namespace replay
