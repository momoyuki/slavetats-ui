#include "textures/D3D11TextureManager.h"

#include <utility>

namespace stui::textures {
namespace {

std::shared_ptr<ID3D11Device> retainDevice(ID3D11Device* device) {
    if (device == nullptr) {
        return {};
    }

    device->AddRef();
    return std::shared_ptr<ID3D11Device>(device, [](ID3D11Device* retainedDevice) {
        retainedDevice->Release();
    });
}

}  // namespace

D3D11TextureManager::D3D11TextureManager(
    ID3D11Device* device,
    std::size_t capacity,
    TextureCacheDuration idleTtl,
    std::size_t maxBytes)
    : m_device(retainDevice(device)),
      m_textures(capacity, idleTtl, maxBytes,
          [](const D3D11Texture& texture) { return texture.byteSize; }) {}

D3D11ManagedTextureResult D3D11TextureManager::getOrLoad(
    std::string_view texturePath,
    std::span<const std::uint8_t> ddsBytes,
    TextureCacheTimePoint now) {
    return m_textures.getOrLoad(
        texturePath,
        ddsBytes,
        [device = m_device.get()](std::span<const std::uint8_t> bytes) {
            auto uploaded = uploadDdsTexture(device, bytes);
            if (!uploaded) {
                return std::shared_ptr<D3D11Texture>{};
            }
            return std::make_shared<D3D11Texture>(std::move(*uploaded));
        },
        now);
}

std::shared_ptr<D3D11Texture> D3D11TextureManager::find(
    std::string_view texturePath,
    TextureCacheTimePoint now) {
    return m_textures.find(texturePath, now);
}

void D3D11TextureManager::pruneExpired(TextureCacheTimePoint now) {
    m_textures.pruneExpired(now);
}

void D3D11TextureManager::clear() noexcept {
    m_textures.clear();
}

std::size_t D3D11TextureManager::size() const noexcept {
    return m_textures.size();
}

}  // namespace stui::textures
