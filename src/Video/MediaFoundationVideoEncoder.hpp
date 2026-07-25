#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief Windows Media Foundation H.264/MP4 encoder.
 *
 * Accepts top-down RGBA8 frames. Odd source dimensions are padded to the next
 * even size because the H.264 NV12 input format requires 2x2 chroma blocks.
 */
class MediaFoundationVideoEncoder {
public:
    MediaFoundationVideoEncoder();
    ~MediaFoundationVideoEncoder();

    MediaFoundationVideoEncoder(const MediaFoundationVideoEncoder&) = delete;
    MediaFoundationVideoEncoder& operator=(
        const MediaFoundationVideoEncoder&) = delete;

    bool begin(const std::filesystem::path& outputPath,
               uint32_t sourceWidth,
               uint32_t sourceHeight,
               int fps,
               uint32_t bitrateBitsPerSecond,
               std::string* error = nullptr);
    bool writeRgbaFrame(const std::vector<uint8_t>& rgba,
                        uint64_t frameIndex,
                        std::string* error = nullptr);
    bool finalize(std::string* error = nullptr);
    void abort();

    bool isActive() const;
    uint32_t sourceWidth() const;
    uint32_t sourceHeight() const;
    uint32_t videoWidth() const;
    uint32_t videoHeight() const;
    const std::filesystem::path& outputPath() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
