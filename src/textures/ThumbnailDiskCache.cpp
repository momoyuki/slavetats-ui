#include "textures/ThumbnailDiskCache.h"

#include "textures/TextureResolver.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>

namespace stui::textures {
namespace {

constexpr std::uint64_t kHashBasis = 14695981039346656037ULL;
constexpr std::uint64_t kHashPrime = 1099511628211ULL;
constexpr std::uintmax_t kMaxEntryBytes = 2 * 1024 * 1024;

std::uint64_t appendHash(std::uint64_t hash, std::string_view text) {
    for (const unsigned char character : text) {
        hash = (hash ^ character) * kHashPrime;
    }
    return hash;
}

std::uint64_t fileStamp(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return 0;
    const auto time = std::filesystem::last_write_time(path, error);
    if (error) return 0;
    return static_cast<std::uint64_t>(size) ^
        static_cast<std::uint64_t>(time.time_since_epoch().count());
}

std::uint64_t archiveStamp(const std::filesystem::path& looseRoot) {
    auto leaf = looseRoot.filename().string();
    std::ranges::transform(leaf, leaf.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (leaf != "slavetats") return 0;
    const auto dataRoot = looseRoot.parent_path().parent_path().parent_path().parent_path();
    std::error_code error;
    std::uint64_t stamp = kHashBasis;
    for (std::filesystem::directory_iterator it(dataRoot, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || error) continue;
        auto extension = it->path().extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (extension != ".bsa") continue;
        stamp ^= appendHash(kHashBasis, it->path().filename().string()) ^ fileStamp(it->path());
    }
    return stamp;
}

std::uintmax_t cachedBytes(const std::filesystem::path& directory) {
    std::error_code error;
    std::uintmax_t total = 0;
    for (std::filesystem::directory_iterator it(directory, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || error || it->path().extension() != ".dds") continue;
        total += it->file_size(error);
    }
    return error ? 0 : total;
}

}  // namespace

ThumbnailDiskCache::ThumbnailDiskCache(std::filesystem::path directory,
    std::filesystem::path looseRoot, std::uintmax_t maxBytes)
    : m_directory(std::move(directory)), m_looseRoot(std::move(looseRoot)),
      m_archiveStamp(archiveStamp(m_looseRoot)), m_maxBytes(maxBytes),
      m_estimatedBytes(cachedBytes(m_directory)) {}

std::filesystem::path ThumbnailDiskCache::entryPath(std::string_view texturePath) const {
    const auto normalized = TextureResolver::normalize(texturePath);
    if (!normalized || m_directory.empty()) return {};
    std::string canonical = *normalized;
    std::ranges::transform(canonical, canonical.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    const auto loosePath = m_looseRoot / std::filesystem::path(*normalized);
    std::error_code error;
    const bool loose = std::filesystem::is_regular_file(loosePath, error) && !error;
    const auto stamp = loose ? fileStamp(loosePath) : m_archiveStamp;
    auto hash = appendHash(kHashBasis, "thumbnail-v1:");
    hash = appendHash(hash, canonical);
    hash = appendHash(hash, loose ? ":loose:" : ":archive:");
    hash = appendHash(hash, std::to_string(stamp));
    std::ostringstream name;
    name << std::hex << std::setw(16) << std::setfill('0') << hash << ".dds";
    return m_directory / name.str();
}

std::optional<std::vector<std::uint8_t>> ThumbnailDiskCache::load(
    std::string_view texturePath) const {
    const auto path = entryPath(texturePath);
    if (path.empty()) return std::nullopt;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > kMaxEntryBytes) return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad() || bytes.size() != size) return std::nullopt;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), error);
    return bytes;
}

void ThumbnailDiskCache::store(std::string_view texturePath,
    std::span<const std::uint8_t> ddsBytes) {
    if (ddsBytes.empty() || ddsBytes.size() > kMaxEntryBytes) return;
    const auto path = entryPath(texturePath);
    if (path.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(m_directory, error);
    if (error) return;
    auto previousBytes = std::filesystem::file_size(path, error);
    if (error) {
        previousBytes = 0;
        error.clear();
    }
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return;
        output.write(reinterpret_cast<const char*>(ddsBytes.data()), ddsBytes.size());
        output.flush();
        if (!output) {
            std::filesystem::remove(temporary, error);
            return;
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        error.clear();
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (error) std::filesystem::remove(temporary, error);
    }
    if (!error) {
        m_estimatedBytes -= std::min(m_estimatedBytes, previousBytes);
        m_estimatedBytes += ddsBytes.size();
        if (m_estimatedBytes > m_maxBytes) prune();
    }
}

void ThumbnailDiskCache::prune() {
    std::error_code error;
    std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> files;
    std::uintmax_t total = 0;
    for (std::filesystem::directory_iterator it(m_directory, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || error || it->path().extension() != ".dds") continue;
        const auto size = it->file_size(error);
        if (error) break;
        total += size;
        files.emplace_back(it->path(), it->last_write_time(error));
        if (error) break;
    }
    if (error || total <= m_maxBytes) return;
    std::ranges::sort(files, {}, &std::pair<std::filesystem::path,
        std::filesystem::file_time_type>::second);
    const auto target = m_maxBytes >= 1024 * 1024 ? m_maxBytes * 3 / 4 : m_maxBytes;
    for (const auto& [path, time] : files) {
        if (total <= target) break;
        const auto size = std::filesystem::file_size(path, error);
        if (error) break;
        if (std::filesystem::remove(path, error) && !error) total -= size;
        if (error) break;
    }
    m_estimatedBytes = total;
}

bool ThumbnailDiskCache::clear() {
    if (m_directory.empty()) return false;
    std::error_code error;
    if (!std::filesystem::exists(m_directory, error)) {
        if (!error) m_estimatedBytes = 0;
        return !error;
    }
    for (std::filesystem::directory_iterator it(m_directory, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || error) continue;
        const auto extension = it->path().extension();
        if (extension == ".dds" || extension == ".tmp") {
            std::filesystem::remove(it->path(), error);
        }
    }
    if (!error) m_estimatedBytes = 0;
    return !error;
}

}  // namespace stui::textures
