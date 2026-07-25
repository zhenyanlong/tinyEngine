#include "Video/MediaFoundationVideoEncoder.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#endif

namespace {

void setError(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
}

#ifdef _WIN32
std::string hresultMessage(const std::string& operation, HRESULT result)
{
    char* systemMessage = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER
            | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(result),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&systemMessage), 0, nullptr);

    std::ostringstream message;
    message << operation << " failed (HRESULT 0x"
            << std::hex << std::uppercase
            << static_cast<unsigned long>(result) << ')';
    if (length > 0 && systemMessage) {
        std::string detail(systemMessage, length);
        while (!detail.empty()
               && (detail.back() == '\r' || detail.back() == '\n'
                   || detail.back() == ' ')) {
            detail.pop_back();
        }
        if (!detail.empty())
            message << ": " << detail;
    }
    if (systemMessage)
        LocalFree(systemMessage);
    return message.str();
}

uint8_t clampByte(int value)
{
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}
#endif

} // namespace

struct MediaFoundationVideoEncoder::Impl {
    bool active = false;
    uint32_t sourceWidth = 0;
    uint32_t sourceHeight = 0;
    uint32_t videoWidth = 0;
    uint32_t videoHeight = 0;
    int fps = 0;
    std::filesystem::path outputPath;

#ifdef _WIN32
    bool mediaFoundationStarted = false;
    bool comInitialized = false;
    DWORD streamIndex = 0;
    Microsoft::WRL::ComPtr<IMFSinkWriter> writer;
    std::vector<uint8_t> nv12;

    void shutdownRuntime()
    {
        writer.Reset();
        if (mediaFoundationStarted) {
            MFShutdown();
            mediaFoundationStarted = false;
        }
        if (comInitialized) {
            CoUninitialize();
            comInitialized = false;
        }
        active = false;
    }

    void convertRgbaToNv12(const std::vector<uint8_t>& rgba)
    {
        const size_t yPlaneBytes =
            static_cast<size_t>(videoWidth) * videoHeight;
        nv12.resize(yPlaneBytes + yPlaneBytes / 2u);

        auto sourcePixel = [&](uint32_t x, uint32_t y) {
            x = std::min(x, sourceWidth - 1u);
            y = std::min(y, sourceHeight - 1u);
            return rgba.data()
                + (static_cast<size_t>(y) * sourceWidth + x) * 4u;
        };

        for (uint32_t y = 0; y < videoHeight; ++y) {
            uint8_t* outputRow =
                nv12.data() + static_cast<size_t>(y) * videoWidth;
            for (uint32_t x = 0; x < videoWidth; ++x) {
                const uint8_t* pixel = sourcePixel(x, y);
                const int r = pixel[0];
                const int g = pixel[1];
                const int b = pixel[2];
                outputRow[x] = clampByte(
                    ((47 * r + 157 * g + 16 * b + 128) >> 8) + 16);
            }
        }

        uint8_t* uvPlane = nv12.data() + yPlaneBytes;
        for (uint32_t y = 0; y < videoHeight; y += 2u) {
            uint8_t* outputRow =
                uvPlane + static_cast<size_t>(y / 2u) * videoWidth;
            for (uint32_t x = 0; x < videoWidth; x += 2u) {
                int r = 0;
                int g = 0;
                int b = 0;
                for (uint32_t dy = 0; dy < 2u; ++dy) {
                    for (uint32_t dx = 0; dx < 2u; ++dx) {
                        const uint8_t* pixel = sourcePixel(x + dx, y + dy);
                        r += pixel[0];
                        g += pixel[1];
                        b += pixel[2];
                    }
                }
                r = (r + 2) / 4;
                g = (g + 2) / 4;
                b = (b + 2) / 4;
                outputRow[x] = clampByte(
                    ((-26 * r - 87 * g + 112 * b + 128) >> 8) + 128);
                outputRow[x + 1u] = clampByte(
                    ((112 * r - 102 * g - 10 * b + 128) >> 8) + 128);
            }
        }
    }
#endif
};

MediaFoundationVideoEncoder::MediaFoundationVideoEncoder()
    : impl_(std::make_unique<Impl>())
{
}

MediaFoundationVideoEncoder::~MediaFoundationVideoEncoder()
{
    abort();
}

bool MediaFoundationVideoEncoder::begin(
    const std::filesystem::path& outputPath,
    uint32_t sourceWidth,
    uint32_t sourceHeight,
    int fps,
    uint32_t bitrateBitsPerSecond,
    std::string* error)
{
    if (error)
        error->clear();
    if (impl_->active) {
        setError(error, "A video encoder session is already active");
        return false;
    }
    if (sourceWidth == 0 || sourceHeight == 0) {
        setError(error, "Video dimensions must be greater than zero");
        return false;
    }
    if (fps < 1 || fps > 240) {
        setError(error, "Video FPS must be in the range 1..240");
        return false;
    }
    if (bitrateBitsPerSecond == 0) {
        setError(error, "Video bitrate must be greater than zero");
        return false;
    }

#ifndef _WIN32
    setError(error, "MP4 recording requires Windows Media Foundation");
    return false;
#else
    const uint64_t paddedWidth =
        (static_cast<uint64_t>(sourceWidth) + 1u) & ~uint64_t(1u);
    const uint64_t paddedHeight =
        (static_cast<uint64_t>(sourceHeight) + 1u) & ~uint64_t(1u);
    if (paddedWidth > std::numeric_limits<uint32_t>::max()
        || paddedHeight > std::numeric_limits<uint32_t>::max()) {
        setError(error, "Video dimensions are too large");
        return false;
    }
    const uint64_t pixelCount = paddedWidth * paddedHeight;
    if (pixelCount > std::numeric_limits<size_t>::max() / 2u
        || pixelCount
            > static_cast<uint64_t>(std::numeric_limits<DWORD>::max())
                * 2u / 3u) {
        setError(error, "Video dimensions are too large");
        return false;
    }

    impl_->sourceWidth = sourceWidth;
    impl_->sourceHeight = sourceHeight;
    impl_->videoWidth = static_cast<uint32_t>(paddedWidth);
    impl_->videoHeight = static_cast<uint32_t>(paddedHeight);
    impl_->fps = fps;
    impl_->outputPath = std::filesystem::absolute(outputPath);

    std::error_code directoryError;
    std::filesystem::create_directories(
        impl_->outputPath.parent_path(), directoryError);
    if (directoryError) {
        setError(error, "Cannot create video output directory: "
            + directoryError.message());
        return false;
    }

    const HRESULT comResult =
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(comResult)) {
        impl_->comInitialized = true;
    } else if (comResult != RPC_E_CHANGED_MODE) {
        setError(error, hresultMessage("CoInitializeEx", comResult));
        return false;
    }

    HRESULT result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(result)) {
        setError(error, hresultMessage("MFStartup", result));
        impl_->shutdownRuntime();
        return false;
    }
    impl_->mediaFoundationStarted = true;

    Microsoft::WRL::ComPtr<IMFAttributes> attributes;
    result = MFCreateAttributes(&attributes, 2);
    if (SUCCEEDED(result)) {
        result = attributes->SetUINT32(
            MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    }
    if (SUCCEEDED(result)) {
        result = attributes->SetUINT32(
            MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    }
    if (FAILED(result)) {
        setError(error, hresultMessage(
            "Creating Media Foundation encoder attributes", result));
        impl_->shutdownRuntime();
        return false;
    }

    result = MFCreateSinkWriterFromURL(
        impl_->outputPath.c_str(), nullptr, attributes.Get(),
        &impl_->writer);
    if (FAILED(result)) {
        setError(error, hresultMessage("MFCreateSinkWriterFromURL", result));
        impl_->shutdownRuntime();
        return false;
    }

    Microsoft::WRL::ComPtr<IMFMediaType> outputType;
    result = MFCreateMediaType(&outputType);
    if (SUCCEEDED(result))
        result = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(result))
        result = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    if (SUCCEEDED(result))
        result = outputType->SetUINT32(
            MF_MT_AVG_BITRATE, bitrateBitsPerSecond);
    if (SUCCEEDED(result))
        result = outputType->SetUINT32(
            MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(result)) {
        result = MFSetAttributeSize(
            outputType.Get(), MF_MT_FRAME_SIZE,
            impl_->videoWidth, impl_->videoHeight);
    }
    if (SUCCEEDED(result)) {
        result = MFSetAttributeRatio(
            outputType.Get(), MF_MT_FRAME_RATE,
            static_cast<UINT32>(fps), 1);
    }
    if (SUCCEEDED(result))
        result = MFSetAttributeRatio(
            outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(result)) {
        result = impl_->writer->AddStream(
            outputType.Get(), &impl_->streamIndex);
    }
    if (FAILED(result)) {
        setError(error, hresultMessage(
            "Configuring the H.264 output stream", result));
        impl_->shutdownRuntime();
        return false;
    }

    Microsoft::WRL::ComPtr<IMFMediaType> inputType;
    result = MFCreateMediaType(&inputType);
    if (SUCCEEDED(result))
        result = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(result))
        result = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    if (SUCCEEDED(result))
        result = inputType->SetUINT32(
            MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(result)) {
        result = MFSetAttributeSize(
            inputType.Get(), MF_MT_FRAME_SIZE,
            impl_->videoWidth, impl_->videoHeight);
    }
    if (SUCCEEDED(result)) {
        result = MFSetAttributeRatio(
            inputType.Get(), MF_MT_FRAME_RATE,
            static_cast<UINT32>(fps), 1);
    }
    if (SUCCEEDED(result))
        result = MFSetAttributeRatio(
            inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(result)) {
        result = impl_->writer->SetInputMediaType(
            impl_->streamIndex, inputType.Get(), nullptr);
    }
    if (SUCCEEDED(result))
        result = impl_->writer->BeginWriting();
    if (FAILED(result)) {
        setError(error, hresultMessage(
            "Starting the H.264 encoder", result));
        impl_->shutdownRuntime();
        return false;
    }

    impl_->active = true;
    return true;
#endif
}

bool MediaFoundationVideoEncoder::writeRgbaFrame(
    const std::vector<uint8_t>& rgba,
    uint64_t frameIndex,
    std::string* error)
{
    if (error)
        error->clear();
    if (!impl_->active) {
        setError(error, "The video encoder is not active");
        return false;
    }

#ifndef _WIN32
    setError(error, "MP4 recording requires Windows Media Foundation");
    return false;
#else
    const uint64_t expectedBytes =
        static_cast<uint64_t>(impl_->sourceWidth)
        * impl_->sourceHeight * 4u;
    if (rgba.size() != expectedBytes) {
        setError(error, "Captured RGBA frame size does not match the video");
        return false;
    }

    impl_->convertRgbaToNv12(rgba);

    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    HRESULT result = MFCreateMemoryBuffer(
        static_cast<DWORD>(impl_->nv12.size()), &buffer);
    BYTE* destination = nullptr;
    if (SUCCEEDED(result))
        result = buffer->Lock(&destination, nullptr, nullptr);
    if (SUCCEEDED(result)) {
        std::memcpy(
            destination, impl_->nv12.data(), impl_->nv12.size());
        result = buffer->Unlock();
        destination = nullptr;
    } else if (destination) {
        buffer->Unlock();
    }
    if (SUCCEEDED(result)) {
        result = buffer->SetCurrentLength(
            static_cast<DWORD>(impl_->nv12.size()));
    }

    Microsoft::WRL::ComPtr<IMFSample> sample;
    if (SUCCEEDED(result))
        result = MFCreateSample(&sample);
    if (SUCCEEDED(result))
        result = sample->AddBuffer(buffer.Get());

    constexpr LONGLONG kTicksPerSecond = 10'000'000;
    const LONGLONG sampleTime = static_cast<LONGLONG>(
        frameIndex * kTicksPerSecond
        / static_cast<uint64_t>(impl_->fps));
    const LONGLONG sampleEnd = static_cast<LONGLONG>(
        (frameIndex + 1u) * kTicksPerSecond
        / static_cast<uint64_t>(impl_->fps));
    if (SUCCEEDED(result))
        result = sample->SetSampleTime(sampleTime);
    if (SUCCEEDED(result))
        result = sample->SetSampleDuration(sampleEnd - sampleTime);
    if (SUCCEEDED(result)) {
        result = impl_->writer->WriteSample(
            impl_->streamIndex, sample.Get());
    }
    if (FAILED(result)) {
        setError(error, hresultMessage(
            "Encoding video frame " + std::to_string(frameIndex), result));
        return false;
    }
    return true;
#endif
}

bool MediaFoundationVideoEncoder::finalize(std::string* error)
{
    if (error)
        error->clear();
    if (!impl_->active)
        return true;

#ifndef _WIN32
    setError(error, "MP4 recording requires Windows Media Foundation");
    return false;
#else
    const HRESULT result = impl_->writer->Finalize();
    impl_->shutdownRuntime();
    if (FAILED(result)) {
        setError(error, hresultMessage("Finalizing the MP4 file", result));
        return false;
    }
    return true;
#endif
}

void MediaFoundationVideoEncoder::abort()
{
#ifdef _WIN32
    impl_->shutdownRuntime();
#else
    impl_->active = false;
#endif
}

bool MediaFoundationVideoEncoder::isActive() const
{
    return impl_->active;
}

uint32_t MediaFoundationVideoEncoder::sourceWidth() const
{
    return impl_->sourceWidth;
}

uint32_t MediaFoundationVideoEncoder::sourceHeight() const
{
    return impl_->sourceHeight;
}

uint32_t MediaFoundationVideoEncoder::videoWidth() const
{
    return impl_->videoWidth;
}

uint32_t MediaFoundationVideoEncoder::videoHeight() const
{
    return impl_->videoHeight;
}

const std::filesystem::path& MediaFoundationVideoEncoder::outputPath() const
{
    return impl_->outputPath;
}
