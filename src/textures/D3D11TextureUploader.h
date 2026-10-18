#pragma once

#include <d3d11.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace stui::textures {

enum class D3D11TextureError {
    invalidData,
    missingDevice,
    uploadFailed,
};

struct D3D11Texture {
    std::shared_ptr<ID3D11ShaderResourceView> shaderResourceView;
    std::size_t width{};
    std::size_t height{};
    std::size_t byteSize{};
    struct UvBounds {
        float left{};
        float top{};
        float right{1.0F};
        float bottom{1.0F};
    } visibleBounds;
};

[[nodiscard]] D3D11Texture::UvBounds visibleAlphaBounds(
    std::span<const std::uint8_t> rgba, std::size_t width,
    std::size_t height, std::size_t rowPitch) noexcept;
[[nodiscard]] D3D11Texture::UvBounds visibleBc3AlphaBounds(
    std::span<const std::uint8_t> blocks, std::size_t width,
    std::size_t height, std::size_t rowPitch) noexcept;
[[nodiscard]] std::optional<std::vector<std::uint8_t>> makeCachedThumbnailDds(
    std::span<const std::uint8_t> ddsBytes);

using D3D11TextureResult = std::expected<D3D11Texture, D3D11TextureError>;

[[nodiscard]] D3D11TextureResult uploadDdsTexture(
    ID3D11Device* device,
    std::span<const std::uint8_t> ddsBytes);

}  // namespace stui::textures
