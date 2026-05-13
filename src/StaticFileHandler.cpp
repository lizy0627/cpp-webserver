#include "StaticFileHandler.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#include <zlib.h>

namespace {
constexpr int gzipWindowBits = MAX_WBITS + 16;
constexpr std::size_t gzipOutputChunkSize = 16 * 1024;

std::string stripQueryAndFragment(const std::string& path) {
    const std::size_t end = path.find_first_of("?#");
    return end == std::string::npos ? path : path.substr(0, end);
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }

    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }

    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }

    return -1;
}

bool urlDecode(const std::string& encoded, std::string& decoded) {
    decoded.clear();
    decoded.reserve(encoded.size());

    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] != '%') {
            decoded.push_back(encoded[i]);
            continue;
        }

        if (i + 2 >= encoded.size()) {
            return false;
        }

        const int high = hexValue(encoded[i + 1]);
        const int low = hexValue(encoded[i + 2]);
        if (high == -1 || low == -1) {
            return false;
        }

        decoded.push_back(static_cast<char>((high << 4) | low));
        i += 2;
    }

    return true;
}

bool containsNullByte(const std::string& value) {
    return value.find('\0') != std::string::npos;
}

bool containsBackslash(const std::string& value) {
    return value.find('\\') != std::string::npos;
}

bool hasParentDirectorySegment(const std::filesystem::path& path) {
    return std::any_of(path.begin(), path.end(), [](const std::filesystem::path& segment) {
        return segment == "..";
    });
}

bool isInsideRoot(const std::filesystem::path& rootDirectory, const std::filesystem::path& filePath) {
    const std::filesystem::path relative = filePath.lexically_relative(rootDirectory);
    if (relative.empty()) {
        return filePath == rootDirectory;
    }

    const auto begin = relative.begin();
    return begin == relative.end() || *begin != "..";
}

bool isUnreservedUrlCharacter(unsigned char character) {
    return std::isalnum(character) ||
        character == '-' ||
        character == '.' ||
        character == '_' ||
        character == '~';
}

std::string urlEncodePathSegment(const std::string& value) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;

    for (unsigned char character : value) {
        if (isUnreservedUrlCharacter(character)) {
            encoded << static_cast<char>(character);
            continue;
        }

        encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(character);
    }

    return encoded.str();
}

std::string htmlEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (char character : value) {
        switch (character) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        case '\'':
            escaped += "&#39;";
            break;
        default:
            escaped.push_back(character);
            break;
        }
    }

    return escaped;
}

std::string normalizedDirectoryRequestPath(const std::string& decodedPath) {
    if (decodedPath.empty()) {
        return "/";
    }

    return decodedPath.back() == '/' ? decodedPath : decodedPath + "/";
}

std::string formatFileTime(const std::filesystem::file_time_type& fileTime) {
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t time = std::chrono::system_clock::to_time_t(systemTime);

    std::tm localTime {};
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream formatted;
    formatted << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return formatted.str();
}

struct DirectoryEntryInfo {
    std::string name;
    bool isDirectory = false;
    std::uintmax_t size = 0;
    std::string modifiedTime;
};

std::string extensionLower(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

bool parseUnsignedInteger(const std::string& value, std::uintmax_t& number) {
    if (value.empty()) {
        return false;
    }

    std::uintmax_t parsed = 0;
    for (char character : value) {
        if (!std::isdigit(static_cast<unsigned char>(character))) {
            return false;
        }

        const std::uintmax_t digit = static_cast<std::uintmax_t>(character - '0');
        if (parsed > (std::numeric_limits<std::uintmax_t>::max() - digit) / 10) {
            return false;
        }

        parsed = parsed * 10 + digit;
    }

    number = parsed;
    return true;
}

std::string toHex(std::uintmax_t value) {
    std::ostringstream stream;
    stream << std::hex << value;
    return stream.str();
}

std::uintmax_t fnv1a64(const std::string& value) {
    std::uintmax_t hash = 14695981039346656037ull;
    for (unsigned char character : value) {
        hash ^= static_cast<std::uintmax_t>(character);
        hash *= 1099511628211ull;
    }

    return hash;
}

std::string trimLinearWhitespace(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }

    const std::size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool parseQualityValue(const std::string& value, double& quality) {
    if (value.empty()) {
        return false;
    }

    try {
        std::size_t parsedLength = 0;
        const double parsed = std::stod(value, &parsedLength);
        if (parsedLength != value.size() || parsed < 0.0 || parsed > 1.0) {
            return false;
        }

        quality = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

double acceptEncodingQuality(const std::string& encodingValue) {
    std::size_t parameterStart = 0;
    while (parameterStart < encodingValue.size()) {
        const std::size_t parameterEnd = encodingValue.find(';', parameterStart);
        std::string parameter = trimLinearWhitespace(encodingValue.substr(
            parameterStart,
            parameterEnd == std::string::npos ? std::string::npos : parameterEnd - parameterStart));

        const std::size_t separator = parameter.find('=');
        if (separator != std::string::npos) {
            const std::string name = toLower(trimLinearWhitespace(parameter.substr(0, separator)));
            if (name == "q") {
                double quality = 0.0;
                if (!parseQualityValue(trimLinearWhitespace(parameter.substr(separator + 1)), quality)) {
                    return 0.0;
                }
                return quality;
            }
        }

        if (parameterEnd == std::string::npos) {
            break;
        }

        parameterStart = parameterEnd + 1;
    }

    return 1.0;
}

bool acceptsGzip(const std::optional<std::string>& acceptEncodingHeader) {
    if (!acceptEncodingHeader.has_value()) {
        return false;
    }

    bool wildcardAccepted = false;
    bool gzipMentioned = false;
    std::size_t tokenStart = 0;
    while (tokenStart <= acceptEncodingHeader->size()) {
        const std::size_t tokenEnd = acceptEncodingHeader->find(',', tokenStart);
        const std::string token = trimLinearWhitespace(acceptEncodingHeader->substr(
            tokenStart,
            tokenEnd == std::string::npos ? std::string::npos : tokenEnd - tokenStart));

        if (!token.empty()) {
            const std::size_t parameterStart = token.find(';');
            const std::string encoding = toLower(trimLinearWhitespace(token.substr(0, parameterStart)));
            const double quality = acceptEncodingQuality(token);
            if (encoding == "gzip") {
                gzipMentioned = true;
                if (quality > 0.0) {
                    return true;
                }
            } else if (encoding == "*" && quality > 0.0) {
                wildcardAccepted = true;
            }
        }

        if (tokenEnd == std::string::npos) {
            break;
        }

        tokenStart = tokenEnd + 1;
    }

    return !gzipMentioned && wildcardAccepted;
}

std::string baseContentType(const std::string& contentType) {
    const std::size_t parameterStart = contentType.find(';');
    return toLower(trimLinearWhitespace(contentType.substr(0, parameterStart)));
}

bool isCompressibleContentType(const std::string& contentType) {
    const std::string baseType = baseContentType(contentType);
    return baseType == "text/html" ||
        baseType == "text/css" ||
        baseType == "application/javascript" ||
        baseType == "application/json" ||
        baseType == "text/plain";
}

bool gzipCompress(const std::string& input, std::string& output) {
    z_stream stream {};
    if (deflateInit2(
            &stream,
            Z_DEFAULT_COMPRESSION,
            Z_DEFLATED,
            gzipWindowBits,
            8,
            Z_DEFAULT_STRATEGY) != Z_OK) {
        return false;
    }

    output.clear();
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());

    int result = Z_OK;
    char buffer[gzipOutputChunkSize];
    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer);
        stream.avail_out = static_cast<uInt>(sizeof(buffer));
        result = deflate(&stream, Z_FINISH);
        if (result != Z_OK && result != Z_STREAM_END) {
            deflateEnd(&stream);
            output.clear();
            return false;
        }

        output.append(buffer, sizeof(buffer) - stream.avail_out);
    } while (result != Z_STREAM_END);

    deflateEnd(&stream);
    return true;
}

bool maybeApplyGzip(StaticFileResult& result, const std::optional<std::string>& acceptEncodingHeader) {
    if (!acceptsGzip(acceptEncodingHeader) ||
        !result.dynamicBody ||
        result.partialContent ||
        !isCompressibleContentType(result.contentType)) {
        return false;
    }

    std::string compressed;
    if (!gzipCompress(result.body, compressed)) {
        return false;
    }

    result.body = std::move(compressed);
    result.contentLength = static_cast<std::uintmax_t>(result.body.size());
    result.gzipEncoded = true;
    return true;
}

}

StaticFileHandler::StaticFileHandler(
    std::string rootDirectory,
    bool enableDirectoryListing,
    std::uintmax_t maxCacheableFileSize,
    std::uintmax_t maxCacheCapacity)
    : rootDirectoryReady_(false),
      enableDirectoryListing_(enableDirectoryListing),
      fileCache_(maxCacheableFileSize, maxCacheCapacity) {
    std::error_code error;
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(std::move(rootDirectory), error);
    if (error) {
        return;
    }

    rootDirectory_ = std::filesystem::weakly_canonical(absoluteRoot, error);
    rootDirectoryReady_ = !error;
}

StaticFileResult StaticFileHandler::handle(
    const std::string& requestPath,
    const std::optional<std::string>& rangeHeader,
    const std::optional<std::string>& ifNoneMatchHeader,
    const std::optional<std::string>& acceptEncodingHeader) const {
    std::filesystem::path filePath;
    bool directoryListing = false;
    const StaticFileResult::Status resolvedStatus = resolveRequestPath(requestPath, filePath, directoryListing);
    if (resolvedStatus == StaticFileResult::Status::BadRequest) {
        return {resolvedStatus, "text/plain; charset=utf-8", "Bad Request", {}, 0, false, 0, 0, false, "", false};
    }

    if (resolvedStatus == StaticFileResult::Status::Forbidden) {
        return {resolvedStatus, "text/plain; charset=utf-8", "Forbidden", {}, 0, false, 0, 0, false, "", false};
    }

    if (resolvedStatus == StaticFileResult::Status::NotFound) {
        return {resolvedStatus, "text/plain; charset=utf-8", "Not Found", {}, 0, false, 0, 0, false, "", false};
    }

    if (resolvedStatus == StaticFileResult::Status::Error) {
        return {
            resolvedStatus,
            "text/plain; charset=utf-8",
            "Internal Server Error",
            {},
            0,
            false,
            0,
            0,
            false,
            "",
            false};
    }

    if (directoryListing) {
        StaticFileResult result = buildDirectoryListing(filePath, requestPath);
        if (!rangeHeader.has_value()) {
            maybeApplyGzip(result, acceptEncodingHeader);
        }
        return result;
    }

    std::error_code error;
    const std::uintmax_t fileSize = std::filesystem::file_size(filePath, error);
    if (error) {
        return {
            StaticFileResult::Status::Error,
            "text/plain; charset=utf-8",
            "Internal Server Error",
            {},
            0,
            false,
            0,
            0,
            false,
            "",
            false};
    }

    const std::filesystem::file_time_type lastModified = std::filesystem::last_write_time(filePath, error);
    if (error) {
        return {
            StaticFileResult::Status::Error,
            "text/plain; charset=utf-8",
            "Internal Server Error",
            {},
            0,
            false,
            0,
            0,
            false,
            "",
            false};
    }

    const std::string contentType = contentTypeForPath(filePath);
    const std::string etag = etagForFile(filePath, fileSize, lastModified);
    if (ifNoneMatchHeader.has_value() && ifNoneMatchMatches(*ifNoneMatchHeader, etag)) {
        return {
            StaticFileResult::Status::NotModified,
            contentType,
            "",
            filePath,
            fileSize,
            false,
            0,
            0,
            false,
            etag};
    }

    ParsedRange range;
    const bool hasRange = rangeHeader.has_value();
    if (rangeHeader.has_value()) {
        if (!parseRangeHeader(*rangeHeader, fileSize, range)) {
            return {
                StaticFileResult::Status::RangeNotSatisfiable,
                "text/plain; charset=utf-8",
                "Range Not Satisfiable",
                {},
                fileSize,
                false,
                0,
                0,
                false,
                etag};
        }
    }

    if (fileCache_.canCache(fileSize)) {
        std::optional<FileCache::Entry> cached = fileCache_.get(filePath, fileSize, lastModified, contentType);
        if (!cached.has_value()) {
            return {
                StaticFileResult::Status::Error,
                "text/plain; charset=utf-8",
                "Internal Server Error",
                {},
                0,
                false,
                0,
                0,
                false,
                "",
                false};
        }

        const std::uintmax_t contentOffset = hasRange ? range.start : 0;
        const std::uintmax_t contentLength = hasRange ? range.end - range.start + 1 : fileSize;
        const std::string body = cached->content.substr(
            static_cast<std::size_t>(contentOffset),
            static_cast<std::size_t>(contentLength));

        StaticFileResult result {
            StaticFileResult::Status::Ok,
            contentType,
            body,
            filePath,
            fileSize,
            hasRange,
            contentOffset,
            contentLength,
            true,
            etag};
        if (!hasRange) {
            maybeApplyGzip(result, acceptEncodingHeader);
        }
        return result;
    }

    fileCache_.remove(filePath);

    if (hasRange) {
        return {
            StaticFileResult::Status::Ok,
            contentType,
            "",
            filePath,
            fileSize,
            true,
            range.start,
            range.end - range.start + 1,
            false,
            etag};
    }

    return {
        StaticFileResult::Status::Ok,
        contentType,
        "",
        filePath,
        fileSize,
        false,
        0,
        fileSize,
        false,
        etag};
}

std::string StaticFileHandler::etagForFile(
    const std::filesystem::path& path,
    std::uintmax_t fileSize,
    const std::filesystem::file_time_type& lastModified) {
    const std::uintmax_t modifiedTicks = static_cast<std::uintmax_t>(
        lastModified.time_since_epoch().count());
    const std::uintmax_t pathHash = fnv1a64(path.string());
    return "\"" + toHex(pathHash) + "-" + toHex(fileSize) + "-" + toHex(modifiedTicks) + "\"";
}

bool StaticFileHandler::ifNoneMatchMatches(const std::string& ifNoneMatchHeader, const std::string& etag) {
    std::size_t tokenStart = 0;
    while (tokenStart <= ifNoneMatchHeader.size()) {
        const std::size_t tokenEnd = ifNoneMatchHeader.find(',', tokenStart);
        std::string token = ifNoneMatchHeader.substr(
            tokenStart,
            tokenEnd == std::string::npos ? std::string::npos : tokenEnd - tokenStart);
        token = trimLinearWhitespace(token);

        if (token == "*" || token == etag) {
            return true;
        }

        constexpr char weakPrefix[] = "W/";
        if (token.compare(0, sizeof(weakPrefix) - 1, weakPrefix) == 0 &&
            token.substr(sizeof(weakPrefix) - 1) == etag) {
            return true;
        }

        if (tokenEnd == std::string::npos) {
            break;
        }

        tokenStart = tokenEnd + 1;
    }

    return false;
}

std::string StaticFileHandler::contentTypeForPath(const std::filesystem::path& path) {
    const std::string extension = extensionLower(path);

    if (extension == ".html") {
        return "text/html; charset=utf-8";
    }

    if (extension == ".css") {
        return "text/css; charset=utf-8";
    }

    if (extension == ".js") {
        return "application/javascript; charset=utf-8";
    }

    if (extension == ".txt") {
        return "text/plain; charset=utf-8";
    }

    if (extension == ".png") {
        return "image/png";
    }

    if (extension == ".jpg" || extension == ".jpeg") {
        return "image/jpeg";
    }

    if (extension == ".gif") {
        return "image/gif";
    }

    if (extension == ".svg") {
        return "image/svg+xml";
    }

    if (extension == ".ico") {
        return "image/x-icon";
    }

    if (extension == ".json") {
        return "application/json";
    }

    if (extension == ".pdf") {
        return "application/pdf";
    }

    if (extension == ".wasm") {
        return "application/wasm";
    }

    return "application/octet-stream";
}

bool StaticFileHandler::parseRangeHeader(
    const std::string& rangeHeader,
    std::uintmax_t fileSize,
    ParsedRange& range) {
    constexpr char rangeUnit[] = "bytes=";
    constexpr std::size_t rangeUnitLength = sizeof(rangeUnit) - 1;

    if (rangeHeader.compare(0, rangeUnitLength, rangeUnit) != 0) {
        return false;
    }

    const std::string rangeSpec = rangeHeader.substr(rangeUnitLength);
    if (rangeSpec.empty() || rangeSpec.find(',') != std::string::npos) {
        return false;
    }

    const std::size_t dash = rangeSpec.find('-');
    if (dash == std::string::npos || rangeSpec.find('-', dash + 1) != std::string::npos) {
        return false;
    }

    const std::string first = rangeSpec.substr(0, dash);
    const std::string last = rangeSpec.substr(dash + 1);
    if (first.empty() && last.empty()) {
        return false;
    }

    if (fileSize == 0) {
        return false;
    }

    if (first.empty()) {
        std::uintmax_t suffixLength = 0;
        if (!parseUnsignedInteger(last, suffixLength) || suffixLength == 0) {
            return false;
        }

        range.start = suffixLength >= fileSize ? 0 : fileSize - suffixLength;
        range.end = fileSize - 1;
        return true;
    }

    std::uintmax_t start = 0;
    if (!parseUnsignedInteger(first, start)) {
        return false;
    }

    std::uintmax_t end = fileSize - 1;
    if (!last.empty() && !parseUnsignedInteger(last, end)) {
        return false;
    }

    if (start >= fileSize || start > end) {
        return false;
    }

    range.start = start;
    range.end = std::min(end, fileSize - 1);
    return true;
}

StaticFileResult StaticFileHandler::buildDirectoryListing(
    const std::filesystem::path& directoryPath,
    const std::string& requestPath) const {
    const std::string cleanPath = stripQueryAndFragment(requestPath);
    std::string decodedPath;
    if (!urlDecode(cleanPath, decodedPath)) {
        return {
            StaticFileResult::Status::BadRequest,
            "text/plain; charset=utf-8",
            "Bad Request",
            {},
            0,
            false,
            0,
            0,
            false,
            "",
            false};
    }

    const std::string displayPath = normalizedDirectoryRequestPath(decodedPath);
    std::vector<DirectoryEntryInfo> entries;

    std::error_code error;
    std::filesystem::directory_iterator iterator(
        directoryPath,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    if (error) {
        return {
            StaticFileResult::Status::Error,
            "text/plain; charset=utf-8",
            "Internal Server Error",
            {},
            0,
            false,
            0,
            0,
            false,
            "",
            false};
    }

    for (const std::filesystem::directory_entry& entry : iterator) {
        const std::filesystem::path entryPath = std::filesystem::weakly_canonical(entry.path(), error);
        if (error) {
            error.clear();
            continue;
        }

        if (!isInsideRoot(rootDirectory_, entryPath)) {
            continue;
        }

        DirectoryEntryInfo info;
        info.name = entry.path().filename().string();
        info.isDirectory = std::filesystem::is_directory(entryPath, error);
        if (error) {
            error.clear();
            continue;
        }

        if (!info.isDirectory) {
            info.size = std::filesystem::file_size(entryPath, error);
            if (error) {
                error.clear();
                info.size = 0;
            }
        }

        const std::filesystem::file_time_type modified = std::filesystem::last_write_time(entryPath, error);
        info.modifiedTime = error ? "-" : formatFileTime(modified);
        error.clear();
        entries.push_back(std::move(info));
    }

    std::sort(entries.begin(), entries.end(), [](const DirectoryEntryInfo& left, const DirectoryEntryInfo& right) {
        if (left.isDirectory != right.isDirectory) {
            return left.isDirectory && !right.isDirectory;
        }

        return left.name < right.name;
    });

    std::ostringstream body;
    body << "<!doctype html>\n"
         << "<html lang=\"en\">\n"
         << "<head>\n"
         << "<meta charset=\"utf-8\">\n"
         << "<title>Index of " << htmlEscape(displayPath) << "</title>\n"
         << "<style>"
         << "body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:2rem;color:#1f2937;}"
         << "table{border-collapse:collapse;width:100%;max-width:960px;}"
         << "th,td{border-bottom:1px solid #e5e7eb;padding:.5rem;text-align:left;}"
         << "th{background:#f9fafb;}"
         << "a{color:#075985;text-decoration:none;}"
         << "a:hover{text-decoration:underline;}"
         << "</style>\n"
         << "</head>\n"
         << "<body>\n"
         << "<h1>Index of " << htmlEscape(displayPath) << "</h1>\n"
         << "<table>\n"
         << "<thead><tr><th>Name</th><th>Directory</th><th>Size</th><th>Modified</th></tr></thead>\n"
         << "<tbody>\n";

    for (const DirectoryEntryInfo& entry : entries) {
        const std::string displayName = entry.name + (entry.isDirectory ? "/" : "");
        const std::string href = normalizedDirectoryRequestPath(decodedPath) +
            urlEncodePathSegment(entry.name) +
            (entry.isDirectory ? "/" : "");

        body << "<tr><td><a href=\"" << htmlEscape(href) << "\">"
             << htmlEscape(displayName)
             << "</a></td><td>"
             << (entry.isDirectory ? "yes" : "no")
             << "</td><td>"
             << (entry.isDirectory ? "-" : std::to_string(entry.size))
             << "</td><td>"
             << htmlEscape(entry.modifiedTime)
             << "</td></tr>\n";
    }

    body << "</tbody>\n"
         << "</table>\n"
         << "</body>\n"
         << "</html>\n";

    const std::string listing = body.str();
    return {
        StaticFileResult::Status::Ok,
        "text/html; charset=utf-8",
        listing,
        {},
        0,
        false,
        0,
        static_cast<std::uintmax_t>(listing.size()),
        true,
        "",
        false};
}

StaticFileResult::Status StaticFileHandler::resolveRequestPath(
    const std::string& requestPath,
    std::filesystem::path& filePath,
    bool& directoryListing) const {
    directoryListing = false;

    if (!rootDirectoryReady_) {
        return StaticFileResult::Status::Error;
    }

    const std::string cleanPath = stripQueryAndFragment(requestPath);
    if (cleanPath.empty() || cleanPath.front() != '/' ||
        containsBackslash(cleanPath) ||
        containsNullByte(cleanPath)) {
        return StaticFileResult::Status::BadRequest;
    }

    std::string decodedPath;
    if (!urlDecode(cleanPath, decodedPath) ||
        containsBackslash(decodedPath) ||
        containsNullByte(decodedPath)) {
        return StaticFileResult::Status::BadRequest;
    }

    std::filesystem::path relativePath = decodedPath == "/"
        ? std::filesystem::path()
        : std::filesystem::path(decodedPath.substr(1));

    if (relativePath.is_absolute() ||
        relativePath.has_root_name() ||
        relativePath.has_root_directory() ||
        hasParentDirectorySegment(relativePath)) {
        return StaticFileResult::Status::BadRequest;
    }

    std::error_code error;
    std::filesystem::path resolvedPath = std::filesystem::weakly_canonical(rootDirectory_ / relativePath, error);
    if (error) {
        return StaticFileResult::Status::Error;
    }

    if (!isInsideRoot(rootDirectory_, resolvedPath)) {
        return StaticFileResult::Status::Forbidden;
    }

    if (!std::filesystem::exists(resolvedPath, error)) {
        return error ? StaticFileResult::Status::Error : StaticFileResult::Status::NotFound;
    }

    if (std::filesystem::is_directory(resolvedPath, error)) {
        if (error) {
            return StaticFileResult::Status::Error;
        }

        const std::filesystem::path directoryPath = resolvedPath;
        const std::filesystem::path indexCandidate = directoryPath / "index.html";
        if (std::filesystem::exists(indexCandidate, error)) {
            const std::filesystem::path indexPath = std::filesystem::weakly_canonical(indexCandidate, error);
            if (error) {
                return StaticFileResult::Status::Error;
            }

            if (!isInsideRoot(rootDirectory_, indexPath)) {
                return StaticFileResult::Status::Forbidden;
            }

            resolvedPath = indexPath;
        } else {
            if (error) {
                return StaticFileResult::Status::Error;
            }

            if (!enableDirectoryListing_) {
                return StaticFileResult::Status::Forbidden;
            }

            filePath = directoryPath;
            directoryListing = true;
            return StaticFileResult::Status::Ok;
        }
    }

    if (!std::filesystem::is_regular_file(resolvedPath, error)) {
        if (error) {
            return StaticFileResult::Status::Error;
        }
        return StaticFileResult::Status::Forbidden;
    }

    filePath = resolvedPath;
    return StaticFileResult::Status::Ok;
}
