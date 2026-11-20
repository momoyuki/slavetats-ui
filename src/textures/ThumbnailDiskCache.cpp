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
    auto hash = appendHash(kHashBasis, "thumbnail-v8:");
    hash = appendHash(hash, canonical);
    hash = appendHash(hash, loose ? ":loose:" : ":archive:");
    hash = appendHash(hash, std::to_string(stamp));
    std::ostringstream name;
    name << std::hex << std::setw(16) << std::setfill('0') << hash << ".dds";
    return m_directory / name.str();
}

std::optional<std::vector<std::uint8_t>> ThumbnailDiskCache::load(
    std::string_view texturePath) const {
    return loadPath(entryPath(texturePath));
}

std::optional<std::vector<std::uint8_t>> ThumbnailDiskCache::loadPath(const std::filesystem::path& path) const {
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
    storePath(entryPath(texturePath), ddsBytes);
}

void ThumbnailDiskCache::storePath(const std::filesystem::path& path, std::span<const std::uint8_t> ddsBytes) {
    if (ddsBytes.empty() || ddsBytes.size() > kMaxEntryBytes) return;
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
        if (m_estimatedBytes > m_maxBytes) (void)prune(m_maxBytes);
    }
}

std::optional<std::uint64_t> ThumbnailDiskCache::revision(const TextureResource& resource) const {
    const auto dataRoot = m_looseRoot.parent_path().parent_path().parent_path().parent_path();
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(dataRoot / "textures", error);
    if (error) return {};
    const auto candidate = std::filesystem::weakly_canonical(dataRoot / resource.path(), error);
    if (error) return {};
    const auto relative = std::filesystem::relative(candidate, root, error);
    if (error || relative.is_absolute()) return {};
    bool contained = true;
    for (const auto& part : relative) if (part == "..") contained = false;
    const bool loose = contained && std::filesystem::is_regular_file(candidate, error) && !error;
    const auto stamp = loose ? fileStamp(candidate) : m_archiveStamp;
    auto hash = appendHash(kHashBasis, "thumbnail-v8:");
    hash = appendHash(hash, resource.kind() == TextureResourceKind::external ? "external:" : "catalog:");
    hash = appendHash(hash, resource.path());
    hash = appendHash(hash, loose ? ":loose:" : ":archive:");
    hash = appendHash(hash, std::to_string(stamp));
    return hash;
}

std::filesystem::path ThumbnailDiskCache::entryPath(const TextureResource& resource) const {
    if (m_directory.empty()) return {};
    const auto stamp = revision(resource);
    if (!stamp) return {};
    std::ostringstream name;
    name << std::hex << std::setw(16) << std::setfill('0') << *stamp << ".dds";
    return m_directory / name.str();
}

std::optional<std::vector<std::uint8_t>> ThumbnailDiskCache::load(const TextureResource& resource) const {
    return loadPath(entryPath(resource));
}
void ThumbnailDiskCache::store(const TextureResource& resource, std::span<const std::uint8_t> bytes) {
    storePath(entryPath(resource), bytes);
}

void ThumbnailDiskCache::store(const TextureResource&, std::span<const std::uint8_t> bytes, std::uint64_t revisionValue) {
    if (m_directory.empty()) return;
    std::ostringstream name;
    name << std::hex << std::setw(16) << std::setfill('0') << revisionValue << ".dds";
    storePath(m_directory / name.str(), bytes);
}

bool ThumbnailDiskCache::setMaxBytes(std::uintmax_t maxBytes) {
    if (maxBytes == 0 || m_directory.empty()) return false;
    if (!prune(maxBytes)) return false;
    m_maxBytes = maxBytes;
    return true;
}

bool ThumbnailDiskCache::prune(std::uintmax_t maxBytes) {
    std::error_code error;
    if (!std::filesystem::exists(m_directory, error)) {
        if (!error) m_estimatedBytes = 0;
        return !error;
    }
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
    if (error) return false;
    m_estimatedBytes = total;
    if (total <= maxBytes) return true;
    std::ranges::sort(files, {}, &std::pair<std::filesystem::path,
        std::filesystem::file_time_type>::second);
    const auto target = maxBytes >= 1024 * 1024 ? maxBytes * 3 / 4 : maxBytes;
    for (const auto& [path, time] : files) {
        if (total <= target) break;
        const auto size = std::filesystem::file_size(path, error);
        if (error) break;
        if (std::filesystem::remove(path, error) && !error) total -= size;
        if (error) break;
    }
    m_estimatedBytes = total;
    return !error && total <= maxBytes;
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
