#include "textures/DdsDecoder.h"

#include <algorithm>

namespace stui::textures {

DdsDecodeResult decodeDds(std::span<const std::uint8_t> bytes) {
    DirectX::ScratchImage image;
    DirectX::TexMetadata metadata;
    // Inspect only the header before DirectXTex allocates the source mip chain.
    constexpr std::size_t maxSourcePixels = 4096 * 4096;
    constexpr std::size_t maxSourceBytes = 128 * 1024 * 1024;
    if (bytes.size() > 128 * 1024 * 1024 ||
        FAILED(DirectX::GetMetadataFromDDSMemory(bytes.data(), bytes.size(),
            DirectX::DDS_FLAGS_NONE, metadata)) ||
        metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D ||
        metadata.arraySize != 1 || metadata.IsCubemap() || metadata.depth != 1 ||
        metadata.width == 0 || metadata.height == 0 ||
        metadata.width > 16384 || metadata.height > 16384 ||
        metadata.width > maxSourcePixels / metadata.height ||
        DirectX::IsTypeless(metadata.format) || DirectX::IsDepthStencil(metadata.format) ||
        DirectX::IsPlanar(metadata.format) || DirectX::IsVideo(metadata.format)) {
        return std::unexpected(DdsDecodeError::invalidData);
    }
    std::size_t totalBytes = 0;
    auto width = metadata.width;
    auto height = metadata.height;
    std::size_t maxMipLevels = 1;
    for (auto side = std::max(width, height); side > 1; side >>= 1) ++maxMipLevels;
    if (metadata.mipLevels == 0 || metadata.mipLevels > maxMipLevels)
        return std::unexpected(DdsDecodeError::invalidData);
    for (std::size_t mip = 0; mip < metadata.mipLevels; ++mip) {
        std::size_t rowPitch{}, slicePitch{};
        if (FAILED(DirectX::ComputePitch(metadata.format, width, height,
                rowPitch, slicePitch)) || slicePitch > maxSourceBytes - totalBytes)
            return std::unexpected(DdsDecodeError::invalidData);
        totalBytes += slicePitch;
        width = std::max<std::size_t>(1, width / 2);
        height = std::max<std::size_t>(1, height / 2);
    }
    const HRESULT result = DirectX::LoadFromDDSMemory(
        bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, &metadata, image);
    if (FAILED(result)) {
        return std::unexpected(DdsDecodeError::invalidData);
    }
    return image;
}

}  // namespace stui::textures
