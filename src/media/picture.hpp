#pragma once

#include "media/frame.hpp"
#include "media/vmx.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace replay
{
// The codec of stored pictures (REPLAY_CODEC). Decoding picks the codec from the picture itself
// (a VMX picture has its own header, anything else is taken as JPEG), so a buffer may hold both.
enum class PictureCodec
{
    Jpeg,
    Vmx
};

struct PictureSettings
{
    PictureCodec codec = PictureCodec::Jpeg;
    int jpegQuality = 92;
    VmxSettings vmx;
};

[[nodiscard]] std::string pictureCodecName(PictureCodec codec);

// Encoders: empty when the codec cannot encode (no VMX on this CPU); JPEG throws like encodeJpeg422.
std::vector<std::uint8_t> encodePictureV210(std::uint8_t const* v210, int width, int height, int rowBytes, PictureSettings const& settings);
std::vector<std::uint8_t> encodePicture422(Frame10 const& frame, PictureSettings const& settings);

bool decodePicture422(std::uint8_t const* data, std::size_t size, Frame10& frame);
bool decodePictureToV210(std::uint8_t const* data, std::size_t size, int width, int height, int rowBytes, std::uint8_t* v210);
bool decodePicturePreview(std::uint8_t const* data, std::size_t size, int minWidth, Frame10& frame);

// For the storage budget.
[[nodiscard]] double pictureBytesPerFrameEstimate(int width, int height, PictureSettings const& settings);
[[nodiscard]] double pictureBytesPerHourEstimate(int width, int height, int fpsNum, int fpsDen, PictureSettings const& settings);
} // namespace replay
