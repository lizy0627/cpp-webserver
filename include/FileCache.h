#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

class FileCache {
public:
    struct Entry {
        std::filesystem::path filePath;
        std::uintmax_t fileSize = 0;
        std::filesystem::file_time_type lastModified;
        std::string contentType;
        std::string content;
    };

    explicit FileCache(
        std::uintmax_t maxCacheableFileSize = 64 * 1024,
        std::uintmax_t maxCapacity = 16 * 1024 * 1024);

    bool canCache(std::uintmax_t fileSize) const;
    std::optional<Entry> get(
        const std::filesystem::path& filePath,
        std::uintmax_t fileSize,
        const std::filesystem::file_time_type& lastModified,
        const std::string& contentType);
    void remove(const std::filesystem::path& filePath);
    std::uintmax_t currentSize() const;

private:
    struct CacheRecord {
        Entry entry;
        std::list<std::string>::iterator lruPosition;
    };

    static std::string cacheKey(const std::filesystem::path& filePath);

    std::optional<Entry> readFile(
        const std::filesystem::path& filePath,
        std::uintmax_t fileSize,
        const std::filesystem::file_time_type& lastModified,
        const std::string& contentType) const;
    void touch(std::unordered_map<std::string, CacheRecord>::iterator record);
    void erase(std::unordered_map<std::string, CacheRecord>::iterator record);
    void evictUntilFits(std::uintmax_t bytesNeeded);

    std::uintmax_t maxCacheableFileSize_;
    std::uintmax_t maxCapacity_;
    mutable std::mutex mutex_;
    std::uintmax_t currentSize_;
    std::list<std::string> lru_;
    std::unordered_map<std::string, CacheRecord> entries_;
};
