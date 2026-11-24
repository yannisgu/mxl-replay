#include "media/picture.hpp"

#include "media/jpeg.hpp"

namespace replay
{
std::string pictureCodecName(PictureCodec codec)
{
    return codec == PictureCodec::Vmx ? "vmx" : "jpeg";
}

std::vector<std::uint8_t> encodePictureV210(std::uint8_t const* v210, int width, int height, int rowBytes, PictureSettings const& settings)
{
    if (settings.codec == PictureCodec::Vmx)
    {
        return encodeVmxV210(v210, width, height, rowBytes, settings.vmx);
    }
    return encodeJpegV210(v210, width, height, rowBytes, settings.jpegQuality);
}

std::vector<std::uint8_t> encodePicture422(Frame10 const& frame, PictureSettings const& settings)
{
    if (settings.codec == PictureCodec::Vmx)
    {
        return encodeVmx422(frame, settings.vmx);
    }
    return encodeJpeg422(frame, settings.jpegQuality);
}

bool decodePicture422(std::uint8_t const* data, std::size_t size, Frame10& frame)
{
    if (isVmxPicture(data, size))
    {
        return decodeVmx422(data, size, frame);
    }
    return decodeJpeg422(data, size, frame);
}

bool decodePictureToV210(std::uint8_t const* data, std::size_t size, int width, int height, int rowBytes, std::uint8_t* v210)
{
    if (isVmxPicture(data, size))
    {
        return decodeVmxToV210(data, size, width, height, rowBytes, v210);
    }
    return decodeJpegToV210(data, size, width, height, rowBytes, v210);
}

bool decodePicturePreview(std::uint8_t const* data, std::size_t size, int minWidth, Frame10& frame)
{
    if (isVmxPicture(data, size))
    {
        return decodeVmxPreview(data, size, minWidth, frame);
    }
    return decodeJpegPreview(data, size, minWidth, frame);
}

double pictureBytesPerFrameEstimate(int width, int height, PictureSettings const& settings)
{
    if (settings.codec == PictureCodec::Vmx)
    {
        return vmxBytesPerFrameEstimate(width, height, settings.vmx);
    }
    return bytesPerFrameEstimate(width, height, settings.jpegQuality);
}

double pictureBytesPerHourEstimate(int width, int height, int fpsNum, int fpsDen, PictureSettings const& settings)
{
    if (fpsDen <= 0)
    {
        return 0;
    }
    return pictureBytesPerFrameEstimate(width, height, settings) * static_cast<double>(fpsNum) / static_cast<double>(fpsDen) * 3600.0;
}
} // namespace replay
