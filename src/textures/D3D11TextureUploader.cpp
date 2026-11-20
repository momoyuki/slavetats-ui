#include "textures/D3D11TextureUploader.h"

#include "textures/DdsDecoder.h"

#include <DirectXTex.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace stui::textures {
namespace {

// DDS reserved header words identify our already-framed cache representation.
constexpr std::array<std::uint8_t, 8> kFramedThumbnailTag{'S', 'T', 'U', 'I', 'T', 'H', '8', 0};

D3D11Texture::UvBounds finishVisibleBounds(
    std::size_t left, std::size_t top, std::size_t right, std::size_t bottom,
    std::size_t width, std::size_t height) noexcept {
    if (left == width) return {};

    const auto padX = std::max<std::size_t>(2, (right - left + 9) / 10);
    const auto padY = std::max<std::size_t>(2, (bottom - top + 9) / 10);
    left = left > padX ? left - padX : 0;
    top = top > padY ? top - padY : 0;
    right = std::min(width, right + padX);
    bottom = std::min(height, bottom + padY);

    const auto minWidth = std::max<std::size_t>(1, 1 + (width - 1) / 8);
    const auto minHeight = std::max<std::size_t>(1, 1 + (height - 1) / 8);
    if (right - left < minWidth) {
        const auto center = (left + right) / 2;
        left = center > minWidth / 2 ? center - minWidth / 2 : 0;
        right = std::min(width, left + minWidth);
        left = right - minWidth;
    }
    if (bottom - top < minHeight) {
        const auto center = (top + bottom) / 2;
        top = center > minHeight / 2 ? center - minHeight / 2 : 0;
        bottom = std::min(height, top + minHeight);
        top = bottom - minHeight;
    }
    return {static_cast<float>(left) / width, static_cast<float>(top) / height,
        static_cast<float>(right) / width, static_cast<float>(bottom) / height};
}

bool makeBc3Thumbnail(const DirectX::Image& source, DirectX::ScratchImage& thumbnail,
    D3D11Texture::UvBounds& visibleBounds) {
    constexpr std::size_t maxThumbnailSize = 192;
    constexpr std::size_t maxSourcePixels = 4096 * 4096;
    if (source.width <= maxThumbnailSize && source.height <= maxThumbnailSize)
        return false;
    if (source.width == 0 || source.height == 0 ||
        source.width > maxSourcePixels / source.height ||
        (source.format != DXGI_FORMAT_BC3_UNORM &&
            source.format != DXGI_FORMAT_BC3_UNORM_SRGB)) return false;
    const auto blocksWide = (source.width + 3) / 4;
    const auto blocksHigh = (source.height + 3) / 4;
    if (!source.pixels || source.rowPitch < blocksWide * 16 ||
        source.slicePitch / source.rowPitch < blocksHigh) return false;

    const auto crop = visibleBc3AlphaBounds(
        {source.pixels, source.slicePitch}, source.width, source.height, source.rowPitch);
    const auto cropLeft = static_cast<std::size_t>(crop.left * source.width);
    const auto cropTop = static_cast<std::size_t>(crop.top * source.height);
    const auto cropWidth = std::max<std::size_t>(1,
        static_cast<std::size_t>(crop.right * source.width) - cropLeft);
    const auto cropHeight = std::max<std::size_t>(1,
        static_cast<std::size_t>(crop.bottom * source.height) - cropTop);
    const auto longestSide = std::max(cropWidth, cropHeight);
    const auto targetSize = std::min(maxThumbnailSize, longestSide);
    const auto width = std::max<std::size_t>(1, cropWidth * targetSize / longestSide);
    const auto height = std::max<std::size_t>(1, cropHeight * targetSize / longestSide);
    const auto outputFormat = source.format == DXGI_FORMAT_BC3_UNORM_SRGB
        ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    if (FAILED(thumbnail.Initialize2D(outputFormat, width, height, 1, 1))) return false;

    struct Accumulator {
        std::uint64_t red{}, green{}, blue{}, alphaSum{};
        std::uint8_t maximumAlpha{};
    };
    std::vector<Accumulator> accumulated(width * height);
    const auto expand565 = [](std::uint16_t color) {
        return std::array<std::uint8_t, 3>{
            static_cast<std::uint8_t>(((color >> 11) & 31) * 255 / 31),
            static_cast<std::uint8_t>(((color >> 5) & 63) * 255 / 63),
            static_cast<std::uint8_t>((color & 31) * 255 / 31)};
    };
    for (std::size_t blockY = 0; blockY < blocksHigh; ++blockY) {
        for (std::size_t blockX = 0; blockX < blocksWide; ++blockX) {
            const auto* block = source.pixels + blockY * source.rowPitch + blockX * 16;
            std::uint64_t alphaIndices{};
            for (std::size_t byte = 0; byte < 6; ++byte)
                alphaIndices |= static_cast<std::uint64_t>(block[2 + byte]) << (8 * byte);
            constexpr std::uint64_t allIndexOne = 0x249249249249ULL;
            if ((alphaIndices == 0 && block[0] <= 8) ||
                (alphaIndices == allIndexOne && block[1] <= 8)) continue;
            std::uint8_t alpha[8]{block[0], block[1]};
            if (alpha[0] > alpha[1]) {
                for (std::size_t i = 2; i < 8; ++i)
                    alpha[i] = static_cast<std::uint8_t>(
                        ((8 - i) * alpha[0] + (i - 1) * alpha[1]) / 7);
            } else {
                for (std::size_t i = 2; i < 6; ++i)
                    alpha[i] = static_cast<std::uint8_t>(
                        ((6 - i) * alpha[0] + (i - 1) * alpha[1]) / 5);
                alpha[6] = 0;
                alpha[7] = 255;
            }
            const auto first = expand565(static_cast<std::uint16_t>(
                block[8] | (static_cast<std::uint16_t>(block[9]) << 8)));
            const auto second = expand565(static_cast<std::uint16_t>(
                block[10] | (static_cast<std::uint16_t>(block[11]) << 8)));
            std::array<std::array<std::uint8_t, 3>, 4> colors{first, second};
            for (std::size_t channel = 0; channel < 3; ++channel) {
                colors[2][channel] = static_cast<std::uint8_t>(
                    (2 * first[channel] + second[channel]) / 3);
                colors[3][channel] = static_cast<std::uint8_t>(
                    (first[channel] + 2 * second[channel]) / 3);
            }
            const std::uint32_t colorIndices =
                static_cast<std::uint32_t>(block[12]) |
                (static_cast<std::uint32_t>(block[13]) << 8) |
                (static_cast<std::uint32_t>(block[14]) << 16) |
                (static_cast<std::uint32_t>(block[15]) << 24);
            for (std::size_t y = 0; y < 4 && blockY * 4 + y < source.height; ++y) {
                for (std::size_t x = 0; x < 4 && blockX * 4 + x < source.width; ++x) {
                    const auto pixel = y * 4 + x;
                    const auto opacity = alpha[(alphaIndices >> (pixel * 3)) & 7];
                    if (opacity <= 8) continue;
                    const auto sourceX = blockX * 4 + x;
                    const auto sourceY = blockY * 4 + y;
                    if (sourceX < cropLeft || sourceY < cropTop ||
                        sourceX >= cropLeft + cropWidth ||
                        sourceY >= cropTop + cropHeight) continue;
                    const auto& color = colors[(colorIndices >> (pixel * 2)) & 3];
                    const auto outputX = (sourceX - cropLeft) * width / cropWidth;
                    const auto outputY = (sourceY - cropTop) * height / cropHeight;
                    auto& result = accumulated[outputY * width + outputX];
                    result.red += color[0] * opacity;
                    result.green += color[1] * opacity;
                    result.blue += color[2] * opacity;
                    result.alphaSum += opacity;
                    result.maximumAlpha = std::max(result.maximumAlpha, opacity);
                }
            }
        }
    }
    auto* image = thumbnail.GetImage(0, 0, 0);
    if (!image) return false;
    std::memset(image->pixels, 0, image->slicePitch);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto& value = accumulated[y * width + x];
            auto* pixel = image->pixels + y * image->rowPitch + x * 4;
            if (value.alphaSum == 0) continue;
            pixel[0] = static_cast<std::uint8_t>(value.red / value.alphaSum);
            pixel[1] = static_cast<std::uint8_t>(value.green / value.alphaSum);
            pixel[2] = static_cast<std::uint8_t>(value.blue / value.alphaSum);
            pixel[3] = value.maximumAlpha;
        }
    }
    visibleBounds = {};
    return true;
}

bool makeGenericThumbnail(const DirectX::ScratchImage& source,
    DirectX::ScratchImage& thumbnail, D3D11Texture::UvBounds& visibleBounds) {
    const auto* first = source.GetImage(0, 0, 0);
    if (!first || (first->width <= 192 && first->height <= 192)) return false;
    const auto outputFormat = DirectX::IsSRGB(first->format)
        ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    // Preflight limits source pixels to 16M: at most one 64MiB RGBA scratch.
    // No source-sized crop or premultiplication copies are needed.
    DirectX::ScratchImage converted;
    const auto* rgba = first;
    if (DirectX::IsCompressed(first->format)) {
        if (FAILED(DirectX::Decompress(*first, outputFormat, converted))) return false;
        rgba = converted.GetImage(0, 0, 0);
    } else if (first->format != outputFormat) {
        if (FAILED(DirectX::Convert(*first, outputFormat, DirectX::TEX_FILTER_DEFAULT,
                0.5F, converted))) return false;
        rgba = converted.GetImage(0, 0, 0);
    }
    if (!rgba) return false;
    const auto crop = visibleAlphaBounds({rgba->pixels, rgba->slicePitch},
        rgba->width, rgba->height, rgba->rowPitch);
    const auto left = static_cast<std::size_t>(crop.left * rgba->width);
    const auto top = static_cast<std::size_t>(crop.top * rgba->height);
    const auto cropWidth = std::max<std::size_t>(1,
        static_cast<std::size_t>(crop.right * rgba->width) - left);
    const auto cropHeight = std::max<std::size_t>(1,
        static_cast<std::size_t>(crop.bottom * rgba->height) - top);
    const auto longest = std::max(cropWidth, cropHeight);
    const auto target = std::min<std::size_t>(192, longest);
    const auto width = std::max<std::size_t>(1, cropWidth * target / longest);
    const auto height = std::max<std::size_t>(1, cropHeight * target / longest);
    if (FAILED(thumbnail.Initialize2D(outputFormat, width, height, 1, 1))) return false;
    struct Accumulator {
        std::uint64_t red{}, green{}, blue{}, alpha{}, count{};
    };
    std::vector<Accumulator> values(width * height);
    for (std::size_t y = 0; y < cropHeight; ++y) {
        for (std::size_t x = 0; x < cropWidth; ++x) {
            const auto* pixel = rgba->pixels + (top + y) * rgba->rowPitch + (left + x) * 4;
            auto& value = values[(y * height / cropHeight) * width + x * width / cropWidth];
            value.red += pixel[0] * pixel[3];
            value.green += pixel[1] * pixel[3];
            value.blue += pixel[2] * pixel[3];
            value.alpha += pixel[3];
            ++value.count;
        }
    }
    auto* output = thumbnail.GetImage(0, 0, 0);
    std::memset(output->pixels, 0, output->slicePitch);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto& value = values[y * width + x];
            if (!value.alpha) continue;
            auto* pixel = output->pixels + y * output->rowPitch + x * 4;
            pixel[0] = static_cast<std::uint8_t>(value.red / value.alpha);
            pixel[1] = static_cast<std::uint8_t>(value.green / value.alpha);
            pixel[2] = static_cast<std::uint8_t>(value.blue / value.alpha);
            pixel[3] = static_cast<std::uint8_t>(value.alpha / value.count);
        }
    }
    visibleBounds = {};
    return true;
}

}  // namespace

std::optional<std::vector<std::uint8_t>> makeCachedThumbnailDds(
    std::span<const std::uint8_t> ddsBytes) {
    auto decoded = decodeDds(ddsBytes);
    if (!decoded) return std::nullopt;
    const auto* first = decoded->GetImage(0, 0, 0);
    if (!first) return std::nullopt;
    DirectX::ScratchImage thumbnail;
    D3D11Texture::UvBounds bounds;
    if (!makeBc3Thumbnail(*first, thumbnail, bounds) &&
        !makeGenericThumbnail(*decoded, thumbnail, bounds)) {
        if (first->width <= 192 && first->height <= 192 &&
            ddsBytes.size() <= 2 * 1024 * 1024)
            return std::vector<std::uint8_t>(ddsBytes.begin(), ddsBytes.end());
        return std::nullopt;
    }
    DirectX::Blob blob;
    if (FAILED(DirectX::SaveToDDSMemory(thumbnail.GetImages(),
            thumbnail.GetImageCount(), thumbnail.GetMetadata(),
            DirectX::DDS_FLAGS_NONE, blob))) return std::nullopt;
    const auto* begin = static_cast<const std::uint8_t*>(blob.GetBufferPointer());
    std::vector<std::uint8_t> result(begin, begin + blob.GetBufferSize());
    if (result.size() < 40) return std::nullopt;
    std::copy(kFramedThumbnailTag.begin(), kFramedThumbnailTag.end(), result.begin() + 32);
    return result;
}

D3D11Texture::UvBounds visibleAlphaBounds(
    std::span<const std::uint8_t> rgba, std::size_t width,
    std::size_t height, std::size_t rowPitch) noexcept {
    if (width == 0 || height == 0 || width > std::numeric_limits<std::size_t>::max() / 4 ||
        rowPitch < width * 4 || rgba.size() / rowPitch < height) return {};

    std::size_t left = width;
    std::size_t top = height;
    std::size_t right = 0;
    std::size_t bottom = 0;
    for (std::size_t y = 0; y < height; ++y) {
        const auto* row = rgba.data() + y * rowPitch;
        for (std::size_t x = 0; x < width; ++x) {
            if (row[x * 4 + 3] <= 8) continue;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x + 1);
            bottom = std::max(bottom, y + 1);
        }
    }
    return finishVisibleBounds(left, top, right, bottom, width, height);
}

D3D11Texture::UvBounds visibleBc3AlphaBounds(
    std::span<const std::uint8_t> blocks, std::size_t width,
    std::size_t height, std::size_t rowPitch) noexcept {
    if (width == 0 || height == 0) return {};
    const auto blocksWide = 1 + (width - 1) / 4;
    const auto blocksHigh = 1 + (height - 1) / 4;
    if (blocksWide > std::numeric_limits<std::size_t>::max() / 16 ||
        rowPitch < blocksWide * 16 || blocks.size() / rowPitch < blocksHigh) return {};

    std::size_t left = width;
    std::size_t top = height;
    std::size_t right = 0;
    std::size_t bottom = 0;
    for (std::size_t blockY = 0; blockY < blocksHigh; ++blockY) {
        for (std::size_t blockX = 0; blockX < blocksWide; ++blockX) {
            const auto* block = blocks.data() + blockY * rowPitch + blockX * 16;
            std::uint64_t indices = 0;
            for (std::size_t byte = 0; byte < 6; ++byte) {
                indices |= static_cast<std::uint64_t>(block[byte + 2]) << (byte * 8);
            }
            constexpr std::uint64_t allIndexOne = 0x249249249249ULL;
            if ((indices == 0 && block[0] <= 8) ||
                (indices == allIndexOne && block[1] <= 8)) continue;
            std::uint8_t alpha[8]{block[0], block[1]};
            if (alpha[0] > alpha[1]) {
                for (std::size_t i = 2; i < 8; ++i) {
                    alpha[i] = static_cast<std::uint8_t>(
                        ((8 - i) * alpha[0] + (i - 1) * alpha[1]) / 7);
                }
            } else {
                for (std::size_t i = 2; i < 6; ++i) {
                    alpha[i] = static_cast<std::uint8_t>(
                        ((6 - i) * alpha[0] + (i - 1) * alpha[1]) / 5);
                }
                alpha[6] = 0;
                alpha[7] = 255;
            }
            for (std::size_t y = 0; y < 4 && blockY * 4 + y < height; ++y) {
                for (std::size_t x = 0; x < 4 && blockX * 4 + x < width; ++x) {
                    const auto index = (indices >> ((y * 4 + x) * 3)) & 7;
                    if (alpha[index] <= 8) continue;
                    const auto pixelX = blockX * 4 + x;
                    const auto pixelY = blockY * 4 + y;
                    left = std::min(left, pixelX);
                    top = std::min(top, pixelY);
                    right = std::max(right, pixelX + 1);
                    bottom = std::max(bottom, pixelY + 1);
                }
            }
        }
    }
    return finishVisibleBounds(left, top, right, bottom, width, height);
}

D3D11TextureResult uploadDdsTexture(
    ID3D11Device* device,
    std::span<const std::uint8_t> ddsBytes) {
    if (device == nullptr) {
        return std::unexpected(D3D11TextureError::missingDevice);
    }

    auto decoded = decodeDds(ddsBytes);
    if (!decoded) {
        return std::unexpected(D3D11TextureError::invalidData);
    }

    D3D11Texture texture;
    ID3D11ShaderResourceView* shaderResourceView = nullptr;
    const auto& metadata = decoded->GetMetadata();
    const auto* firstImage = decoded->GetImage(0, 0, 0);
    DirectX::ScratchImage thumbnail;
    D3D11Texture::UvBounds reducedBounds;
    const bool reduced = firstImage &&
        (makeBc3Thumbnail(*firstImage, thumbnail, reducedBounds) ||
            makeGenericThumbnail(*decoded, thumbnail, reducedBounds));
    if (!reduced && (metadata.width > 192 || metadata.height > 192))
        return std::unexpected(D3D11TextureError::invalidData);
    const HRESULT result = DirectX::CreateShaderResourceView(
        device,
        reduced ? thumbnail.GetImages() : decoded->GetImages(),
        reduced ? thumbnail.GetImageCount() : decoded->GetImageCount(),
        reduced ? thumbnail.GetMetadata() : metadata,
        &shaderResourceView);
    if (FAILED(result)) {
        return std::unexpected(D3D11TextureError::uploadFailed);
    }
    texture.shaderResourceView = std::shared_ptr<ID3D11ShaderResourceView>(
        shaderResourceView, [](ID3D11ShaderResourceView* view) {
            view->Release();
        });

    texture.width = reduced ? thumbnail.GetMetadata().width : metadata.width;
    texture.height = reduced ? thumbnail.GetMetadata().height : metadata.height;
    const auto* uploadedImages = reduced ? thumbnail.GetImages() : decoded->GetImages();
    const auto uploadedCount = reduced ? thumbnail.GetImageCount() : decoded->GetImageCount();
    for (std::size_t index = 0; index < uploadedCount; ++index) {
        const auto bytes = uploadedImages[index].slicePitch;
        if (bytes > std::numeric_limits<std::size_t>::max() - texture.byteSize) {
            texture.byteSize = std::numeric_limits<std::size_t>::max();
            break;
        }
        texture.byteSize += bytes;
    }
    if (reduced) texture.visibleBounds = reducedBounds;
    constexpr std::size_t maxAlphaAnalysisPixels = 4096 * 4096;
    const bool preframed = ddsBytes.size() >= 40 &&
        std::equal(kFramedThumbnailTag.begin(), kFramedThumbnailTag.end(), ddsBytes.begin() + 32) &&
        metadata.width <= 192 && metadata.height <= 192 &&
        (metadata.format == DXGI_FORMAT_R8G8B8A8_UNORM ||
            metadata.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    if (!reduced && !preframed && firstImage && firstImage->height > 0 &&
        firstImage->width <= maxAlphaAnalysisPixels / firstImage->height) {
        if (firstImage->format == DXGI_FORMAT_BC3_UNORM ||
            firstImage->format == DXGI_FORMAT_BC3_UNORM_SRGB) {
            texture.visibleBounds = visibleBc3AlphaBounds(
                {firstImage->pixels, firstImage->slicePitch},
                firstImage->width, firstImage->height, firstImage->rowPitch);
        } else {
            DirectX::ScratchImage converted;
            const DirectX::Image* rgbaImage = firstImage;
            if (firstImage->format != DXGI_FORMAT_R8G8B8A8_UNORM) {
                const HRESULT conversion = DirectX::IsCompressed(firstImage->format)
                    ? DirectX::Decompress(*firstImage, DXGI_FORMAT_R8G8B8A8_UNORM, converted)
                    : DirectX::Convert(*firstImage, DXGI_FORMAT_R8G8B8A8_UNORM,
                        DirectX::TEX_FILTER_DEFAULT, 0.0F, converted);
                if (SUCCEEDED(conversion)) rgbaImage = converted.GetImage(0, 0, 0);
                else rgbaImage = nullptr;
            }
            if (rgbaImage && rgbaImage->pixels) {
                texture.visibleBounds = visibleAlphaBounds(
                    {rgbaImage->pixels, rgbaImage->slicePitch},
                    rgbaImage->width, rgbaImage->height, rgbaImage->rowPitch);
            }
        }
    }
    return texture;
}

}  // namespace stui::textures
