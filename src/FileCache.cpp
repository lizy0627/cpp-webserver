#include "FileCache.h"

#include <fstream>
#include <utility>

FileCache::FileCache(std::uintmax_t maxCacheableFileSize, std::uintmax_t maxCapacity)
    : maxCacheableFileSize_(maxCacheableFileSize),
      maxCapacity_(maxCapacity),
      currentSize_(0) {}

bool FileCache::canCache(std::uintmax_t fileSize) const {
    return fileSize < maxCacheableFileSize_ && fileSize <= maxCapacity_;
}

std::optional<FileCache::Entry> FileCache::get(
    const std::filesystem::path& filePath,
    std::uintmax_t fileSize,
    const std::filesystem::file_time_type& lastModified,
    const std::string& contentType) {
    if (!canCache(fileSize)) {
        remove(filePath);
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = cacheKey(filePath);
    const auto existing = entries_.find(key);
    if (existing != entries_.end()) {
        const Entry& entry = existing->second.entry;
        if (entry.fileSize == fileSize &&
            entry.lastModified == lastModified &&
            entry.contentType == contentType) {
            touch(existing);
            return existing->second.entry;
        }

        erase(existing);
    }

    std::optional<Entry> loaded = readFile(filePath, fileSize, lastModified, contentType);
    if (!loaded.has_value()) {
        return std::nullopt;
    }

    evictUntilFits(fileSize);
    lru_.push_front(key);
    CacheRecord record{*loaded, lru_.begin()};
    entries_.emplace(key, std::move(record));
    currentSize_ += fileSize;
    return loaded;
}

void FileCache::remove(const std::filesystem::path& filePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto record = entries_.find(cacheKey(filePath));
    if (record != entries_.end()) {
        erase(record);
    }
}

std::uintmax_t FileCache::currentSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentSize_;
}

std::string FileCache::cacheKey(const std::filesystem::path& filePath) {
    return filePath.string();
}

std::optional<FileCache::Entry> FileCache::readFile(
    const std::filesystem::path& filePath,
    std::uintmax_t fileSize,
    const std::filesystem::file_time_type& lastModified,
    const std::string& contentType) const {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    Entry entry;
    entry.filePath = filePath;
    entry.fileSize = fileSize;
    entry.lastModified = lastModified;
    entry.contentType = contentType;
    entry.content.resize(static_cast<std::size_t>(fileSize));

    if (fileSize > 0) {
        file.read(entry.content.data(), static_cast<std::streamsize>(entry.content.size()));
        if (file.gcount() != static_cast<std::streamsize>(entry.content.size())) {
            return std::nullopt;
        }
    }

    return entry;
}

void FileCache::touch(std::unordered_map<std::string, CacheRecord>::iterator record) {
    lru_.splice(lru_.begin(), lru_, record->second.lruPosition);
    record->second.lruPosition = lru_.begin();
}

void FileCache::erase(std::unordered_map<std::string, CacheRecord>::iterator record) {
    currentSize_ -= record->second.entry.fileSize;
    lru_.erase(record->second.lruPosition);
    entries_.erase(record);
}

void FileCache::evictUntilFits(std::uintmax_t bytesNeeded) {
    while (!lru_.empty() && currentSize_ + bytesNeeded > maxCapacity_) {
        const auto oldest = entries_.find(lru_.back());
        if (oldest == entries_.end()) {
            lru_.pop_back();
            continue;
        }

        erase(oldest);
    }
}
