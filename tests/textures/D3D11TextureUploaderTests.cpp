#include "textures/D3D11TextureUploader.h"

#include <DirectXTex.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

int main() {
    std::array<std::uint8_t, 16 * 16 * 4> sparse{};
    for (std::size_t y = 6; y < 10; ++y) {
        for (std::size_t x = 6; x < 10; ++x) sparse[(y * 16 + x) * 4 + 3] = 255;
    }
    const auto bounds = stui::textures::visibleAlphaBounds(sparse, 16, 16, 16 * 4);
    if (!(bounds.left > 0.0F && bounds.top > 0.0F &&
            bounds.right < 1.0F && bounds.bottom < 1.0F &&
            bounds.right - bounds.left >= 0.125F)) {
        std::cerr << "FAIL sparse alpha should produce a padded, bounded crop\n";
        return 1;
    }
    std::array<std::uint8_t, 64 * 64 * 4> singlePixel{};
    singlePixel[(32 * 64 + 32) * 4 + 3] = 255;
    const auto limited = stui::textures::visibleAlphaBounds(singlePixel, 64, 64, 64 * 4);
    if (limited.right - limited.left < 0.125F ||
        limited.bottom - limited.top < 0.125F) {
        std::cerr << "FAIL tiny mark should not be magnified beyond eightfold\n";
        return 1;
    }
    if (stui::textures::visibleAlphaBounds({}, 16, 16, 16 * 4).right != 1.0F ||
        stui::textures::visibleAlphaBounds(sparse, 16, 16, 3).left != 0.0F) {
        std::cerr << "FAIL empty or invalid image should use the full texture\n";
        return 1;
    }
    sparse.fill(255);
    const auto opaque = stui::textures::visibleAlphaBounds(sparse, 16, 16, 16 * 4);
    if (opaque.left != 0.0F || opaque.top != 0.0F ||
        opaque.right != 1.0F || opaque.bottom != 1.0F) {
        std::cerr << "FAIL opaque image should keep full texture bounds\n";
        return 1;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    D3D_FEATURE_LEVEL featureLevel{};
    const HRESULT deviceResult = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, device.GetAddressOf(), &featureLevel, nullptr);
    if (FAILED(deviceResult)) {
        std::cerr << "FAIL could not create WARP D3D11 device\n";
        return 1;
    }

    DirectX::ScratchImage source;
    if (FAILED(source.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 2, 3, 1, 1))) {
        std::cerr << "FAIL could not create DDS fixture\n";
        return 1;
    }
    DirectX::Blob dds;
    if (FAILED(DirectX::SaveToDDSMemory(
            source.GetImages(), source.GetImageCount(), source.GetMetadata(),
            DirectX::DDS_FLAGS_NONE, dds))) {
        std::cerr << "FAIL could not encode DDS fixture\n";
        return 1;
    }

    const auto bytes = std::span(
        static_cast<const std::uint8_t*>(dds.GetBufferPointer()), dds.GetBufferSize());

    const auto missingDevice = stui::textures::uploadDdsTexture(nullptr, bytes);
    if (missingDevice ||
        missingDevice.error() != stui::textures::D3D11TextureError::missingDevice) {
        std::cerr << "FAIL null device should return missingDevice\n";
        return 1;
    }

    const std::vector<std::uint8_t> malformedDds{1, 2, 3};
    const auto invalidData = stui::textures::uploadDdsTexture(device.Get(), malformedDds);
    if (invalidData ||
        invalidData.error() != stui::textures::D3D11TextureError::invalidData) {
        std::cerr << "FAIL malformed DDS should return invalidData\n";
        return 1;
    }

    std::cout << "PASS uploader rejects missing device and malformed DDS\n";

    const auto texture = stui::textures::uploadDdsTexture(device.Get(), bytes);
    if (!texture || !texture->shaderResourceView ||
        texture->width != 2 || texture->height != 3) {
        std::cerr << "FAIL valid DDS should create a 2x3 shader resource view\n";
        return 1;
    }

    std::cout << "PASS valid DDS creates D3D11 shader resource view\n";

    DirectX::ScratchImage sparseSource;
    if (FAILED(sparseSource.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 32, 32, 1, 1))) {
        std::cerr << "FAIL could not create sparse DDS fixture\n";
        return 1;
    }
    auto* sparseImage = sparseSource.GetImage(0, 0, 0);
    for (std::size_t y = 0; y < 32; ++y) {
        for (std::size_t x = 0; x < 32; ++x) {
            auto* pixel = sparseImage->pixels + y * sparseImage->rowPitch + x * 4;
            pixel[0] = 255;
            pixel[1] = 128;
            pixel[2] = 64;
            pixel[3] = x >= 12 && x < 20 && y >= 12 && y < 20 ? 255 : 0;
        }
    }
    DirectX::ScratchImage compressed;
    if (FAILED(DirectX::Compress(*sparseImage, DXGI_FORMAT_BC3_UNORM,
            DirectX::TEX_COMPRESS_DEFAULT, 0.5F, compressed))) {
        std::cerr << "FAIL could not compress sparse DDS fixture\n";
        return 1;
    }
    const auto* compressedImage = compressed.GetImage(0, 0, 0);
    DirectX::ScratchImage decompressed;
    if (FAILED(DirectX::Decompress(*compressedImage, DXGI_FORMAT_R8G8B8A8_UNORM,
            decompressed))) {
        std::cerr << "FAIL could not decompress sparse DDS fixture\n";
        return 1;
    }
    const auto* decompressedImage = decompressed.GetImage(0, 0, 0);
    const auto directBounds = stui::textures::visibleBc3AlphaBounds(
        {compressedImage->pixels, compressedImage->slicePitch},
        compressedImage->width, compressedImage->height, compressedImage->rowPitch);
    const auto decodedBounds = stui::textures::visibleAlphaBounds(
        {decompressedImage->pixels, decompressedImage->slicePitch},
        decompressedImage->width, decompressedImage->height, decompressedImage->rowPitch);
    if (directBounds.left != decodedBounds.left || directBounds.top != decodedBounds.top ||
        directBounds.right != decodedBounds.right ||
        directBounds.bottom != decodedBounds.bottom) {
        std::cerr << "FAIL BC3 alpha scan should match decompressed RGBA bounds\n";
        return 1;
    }
    std::array<std::uint8_t, 4 * 4 * 16> bc3Blocks{};
    const std::size_t markedBlock = (2 * 4 + 2) * 16;
    bc3Blocks[markedBlock + 2] = 7;  // Alpha index 7 is opaque when endpoints match.
    const auto explicitAlpha = stui::textures::visibleBc3AlphaBounds(
        bc3Blocks, 16, 16, 4 * 16);
    if (explicitAlpha.left <= 0.0F || explicitAlpha.top <= 0.0F ||
        explicitAlpha.right >= 1.0F || explicitAlpha.bottom >= 1.0F ||
        stui::textures::visibleBc3AlphaBounds({}, 16, 16, 4 * 16).left != 0.0F) {
        std::cerr << "FAIL BC3 endpoint ordering or invalid data fallback\n";
        return 1;
    }
    DirectX::Blob compressedDds;
    if (FAILED(DirectX::SaveToDDSMemory(
            compressed.GetImages(), compressed.GetImageCount(), compressed.GetMetadata(),
            DirectX::DDS_FLAGS_NONE, compressedDds))) {
        std::cerr << "FAIL could not encode compressed DDS fixture\n";
        return 1;
    }
    const auto compressedBytes = std::span(
        static_cast<const std::uint8_t*>(compressedDds.GetBufferPointer()),
        compressedDds.GetBufferSize());
    const auto croppedTexture = stui::textures::uploadDdsTexture(device.Get(), compressedBytes);
    if (!croppedTexture || croppedTexture->visibleBounds.left <= 0.0F ||
        croppedTexture->visibleBounds.top <= 0.0F ||
        croppedTexture->visibleBounds.right >= 1.0F ||
        croppedTexture->visibleBounds.bottom >= 1.0F) {
        std::cerr << "FAIL compressed DDS should retain its alpha crop\n";
        return 1;
    }

    DirectX::ScratchImage largeBc3;
    if (FAILED(largeBc3.Initialize2D(DXGI_FORMAT_BC3_UNORM, 1024, 1024, 1, 1))) {
        std::cerr << "FAIL could not create large BC3 fixture\n";
        return 1;
    }
    auto* largeImage = largeBc3.GetImage(0, 0, 0);
    std::memset(largeImage->pixels, 0, largeImage->slicePitch);
    auto* mark = largeImage->pixels + 128 * largeImage->rowPitch + 128 * 16;
    mark[0] = mark[1] = 255;
    mark[9] = 0xF8;  // Opaque red RGB565 endpoint.
    DirectX::Blob largeDds;
    if (FAILED(DirectX::SaveToDDSMemory(
            largeBc3.GetImages(), largeBc3.GetImageCount(), largeBc3.GetMetadata(),
            DirectX::DDS_FLAGS_NONE, largeDds))) {
        std::cerr << "FAIL could not encode large BC3 fixture\n";
        return 1;
    }
    const auto smallPreview = stui::textures::uploadDdsTexture(device.Get(),
        {static_cast<const std::uint8_t*>(largeDds.GetBufferPointer()),
            largeDds.GetBufferSize()});
    const auto expectedLargeBounds = stui::textures::visibleBc3AlphaBounds(
        {largeImage->pixels, largeImage->slicePitch},
        largeImage->width, largeImage->height, largeImage->rowPitch);
    if (!smallPreview || smallPreview->width != 512 || smallPreview->height != 512 ||
        !smallPreview->shaderResourceView ||
        smallPreview->visibleBounds.left != expectedLargeBounds.left ||
        smallPreview->visibleBounds.top != expectedLargeBounds.top ||
        smallPreview->visibleBounds.right != expectedLargeBounds.right ||
        smallPreview->visibleBounds.bottom != expectedLargeBounds.bottom) {
        std::cerr << "FAIL large BC3 should upload a cropped 512px preview\n";
        return 1;
    }
    Microsoft::WRL::ComPtr<ID3D11Resource> previewResource;
    smallPreview->shaderResourceView->GetResource(previewResource.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Texture2D> previewTexture;
    if (FAILED(previewResource.As(&previewTexture))) {
        std::cerr << "FAIL preview resource should be a 2D texture\n";
        return 1;
    }
    D3D11_TEXTURE2D_DESC previewDesc{};
    previewTexture->GetDesc(&previewDesc);
    if (previewDesc.Width != 512 || previewDesc.Height != 512 ||
        previewDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        std::cerr << "FAIL GPU should retain only the small RGBA preview\n";
        return 1;
    }
    auto stagingDesc = previewDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(context.GetAddressOf());
    if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf()))) {
        std::cerr << "FAIL could not inspect reduced GPU texture\n";
        return 1;
    }
    context->CopyResource(staging.Get(), previewTexture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        std::cerr << "FAIL could not read reduced GPU texture\n";
        return 1;
    }
    const auto* markedPixel = static_cast<const std::uint8_t*>(mapped.pData) +
        256 * mapped.RowPitch + 256 * 4;
    const auto* emptyPixel = static_cast<const std::uint8_t*>(mapped.pData);
    const bool correctPixels = markedPixel[0] > 240 && markedPixel[1] < 8 &&
        markedPixel[2] < 8 && markedPixel[3] == 255 && emptyPixel[3] == 0;
    context->Unmap(staging.Get(), 0);
    if (!correctPixels) {
        std::cerr << "FAIL reduced BC3 pixels should retain color and alpha\n";
        return 1;
    }

    return 0;
}
