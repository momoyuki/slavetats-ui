#include "textures/DdsDecoder.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

bool decodesValidDdsMetadata() {
    DirectX::ScratchImage source;
    if (FAILED(source.Initialize2D(
            DXGI_FORMAT_R8G8B8A8_UNORM, 2, 3, 1, 1))) {
        std::cerr << "FAIL could not create DDS fixture\n";
        return false;
    }

    DirectX::Blob dds;
    if (FAILED(DirectX::SaveToDDSMemory(
            source.GetImages(), source.GetImageCount(), source.GetMetadata(),
            DirectX::DDS_FLAGS_NONE, dds))) {
        std::cerr << "FAIL could not encode DDS fixture\n";
        return false;
    }

    const auto bytes = std::span(
        static_cast<const std::uint8_t*>(dds.GetBufferPointer()), dds.GetBufferSize());
    const auto decoded = stui::textures::decodeDds(bytes);
    if (!decoded || decoded->GetMetadata().width != 2 || decoded->GetMetadata().height != 3) {
        std::cerr << "FAIL valid DDS should preserve 2x3 metadata\n";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    {
        // 4K RGBA payload is 64MiB plus its header; header bytes must not reject it.
        DirectX::ScratchImage rgba;
        DirectX::Blob blob;
        if (FAILED(rgba.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 4096, 4096, 1, 1)) ||
            FAILED(DirectX::SaveToDDSMemory(rgba.GetImages(), rgba.GetImageCount(),
                rgba.GetMetadata(), DirectX::DDS_FLAGS_NONE, blob)) ||
            !stui::textures::decodeDds({static_cast<const std::uint8_t*>(blob.GetBufferPointer()),
                blob.GetBufferSize()})) {
            std::cerr << "FAIL bounded 4K RGBA must include room for DDS header\n";
            return 1;
        }
    }

    for (const auto width : {8u, 4097u}) {
        DirectX::ScratchImage source;
        if (FAILED(source.Initialize2D(DXGI_FORMAT_BC1_UNORM, width,
                width == 8 ? 8 : 4097, width == 8 ? 2 : 1, 1))) return 1;
        DirectX::Blob blob;
        if (FAILED(DirectX::SaveToDDSMemory(source.GetImages(), source.GetImageCount(),
                source.GetMetadata(), DirectX::DDS_FLAGS_NONE, blob))) return 1;
        if (stui::textures::decodeDds({static_cast<const std::uint8_t*>(
                blob.GetBufferPointer()), blob.GetBufferSize()})) {
            std::cerr << "FAIL array/excessive DDS must be rejected before decode\n";
            return 1;
        }
    }

    for (const bool cube : {false, true}) {
        DirectX::ScratchImage unsupported;
        const auto initialized = cube
            ? unsupported.InitializeCube(DXGI_FORMAT_BC1_UNORM, 4, 4, 1, 1)
            : unsupported.Initialize3D(DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 4, 1);
        DirectX::Blob blob;
        if (FAILED(initialized) || FAILED(DirectX::SaveToDDSMemory(
                unsupported.GetImages(), unsupported.GetImageCount(), unsupported.GetMetadata(),
                DirectX::DDS_FLAGS_NONE, blob))) return 1;
        if (stui::textures::decodeDds({static_cast<const std::uint8_t*>(
                blob.GetBufferPointer()), blob.GetBufferSize()})) {
            std::cerr << "FAIL cube/volume DDS must not reach thumbnail decode\n";
            return 1;
        }
    }
    const std::vector<std::uint8_t> invalidDds{0x44, 0x44, 0x53, 0x20};
    const auto decoded = stui::textures::decodeDds(invalidDds);
    if (decoded || decoded.error() != stui::textures::DdsDecodeError::invalidData) {
        std::cerr << "FAIL broken DDS should return invalidData\n";
        return 1;
    }
    std::cout << "PASS broken DDS returns invalidData\n";
    if (!decodesValidDdsMetadata()) {
        return 1;
    }
    std::cout << "PASS valid DDS preserves metadata\n";
    return 0;
}
