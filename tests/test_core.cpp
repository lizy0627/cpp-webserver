#include "CommandLineOptions.h"
#include "Config.h"
#include "FileCache.h"
#include "HttpParser.h"
#include "HttpResponse.h"
#include "Router.h"
#include "StaticFileHandler.h"
#include "ThreadPool.h"
#include "WebSocketHandshake.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <zlib.h>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expectParseFailure(const std::string& rawRequest, const char* message) {
    HttpRequest request;
    expect(!HttpParser::parse(rawRequest, request), message);
}

void expectResponseConnectionHeader(bool keepAlive, const std::string& expectedHeader, const char* message) {
    HttpResponse response(200, "OK");
    response.setKeepAlive(keepAlive);

    const std::string raw = response.toString();
    expect(raw.find(expectedHeader) != std::string::npos, message);
}

std::string responseHeaders(const std::string& rawResponse) {
    const std::size_t separator = rawResponse.find("\r\n\r\n");
    if (separator == std::string::npos) {
        throw std::runtime_error("response header separator not found");
    }

    return rawResponse.substr(0, separator + 4);
}

std::string responseBody(const std::string& rawResponse) {
    const std::size_t separator = rawResponse.find("\r\n\r\n");
    if (separator == std::string::npos) {
        throw std::runtime_error("response header separator not found");
    }

    return rawResponse.substr(separator + 4);
}

void writeFile(const std::filesystem::path& path, const std::string& body) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to create test file");
    }
    file << body;
}

std::string gunzip(const std::string& compressed) {
    z_stream stream {};
    if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK) {
        throw std::runtime_error("failed to initialize gzip inflater");
    }

    std::string output;
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());

    int result = Z_OK;
    char buffer[4096];
    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer);
        stream.avail_out = static_cast<uInt>(sizeof(buffer));
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&stream);
            throw std::runtime_error("failed to inflate gzip body");
        }

        output.append(buffer, sizeof(buffer) - stream.avail_out);
    } while (result != Z_STREAM_END);

    inflateEnd(&stream);
    return output;
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() / uniqueName()) {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    static std::filesystem::path uniqueName() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::path("cpp-webserver-static-tests-" + std::to_string(now));
    }

    std::filesystem::path path_;
};

bool parseArguments(
    std::vector<std::string> arguments,
    CommandLineOptions& options,
    std::string& errorMessage) {
    std::vector<char*> argv;
    argv.reserve(arguments.size());
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }

    return parseCommandLineOptions(static_cast<int>(argv.size()), argv.data(), options, errorMessage);
}

void testHttpParserParsesGetRootHttp11() {
    HttpRequest request;
    const bool parsed = HttpParser::parse(
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n",
        request);

    expect(parsed, "request should parse");
    expect(request.method == "GET", "method should be GET");
    expect(request.path == "/", "path should be /");
    expect(request.version == "HTTP/1.1", "version should be HTTP/1.1");
    expect(request.headers["host"] == "localhost", "Host header should parse");
    expect(request.keepAlive, "HTTP/1.1 should keep alive by default");
    expectResponseConnectionHeader(
        request.keepAlive,
        "Connection: keep-alive\r\n",
        "HTTP/1.1 default response should keep alive");
}

void testHttpParserHttp11ConnectionCloseDisablesKeepAlive() {
    HttpRequest request;
    const bool parsed = HttpParser::parse(
        "GET / HTTP/1.1\r\n"
        "Connection: close\r\n"
        "\r\n",
        request);

    expect(parsed, "HTTP/1.1 Connection: close request should parse");
    expect(!request.keepAlive, "HTTP/1.1 Connection: close should disable keep-alive");
    expectResponseConnectionHeader(
        request.keepAlive,
        "Connection: close\r\n",
        "HTTP/1.1 Connection: close response should close");
}

void testHttpParserHttp10ClosesByDefault() {
    HttpRequest request;
    const bool parsed = HttpParser::parse(
        "GET / HTTP/1.0\r\n"
        "\r\n",
        request);

    expect(parsed, "HTTP/1.0 request should parse");
    expect(!request.keepAlive, "HTTP/1.0 should close by default");
    expectResponseConnectionHeader(
        request.keepAlive,
        "Connection: close\r\n",
        "HTTP/1.0 default response should close");
}

void testHttpParserHttp10ConnectionKeepAliveEnablesKeepAlive() {
    HttpRequest request;
    const bool parsed = HttpParser::parse(
        "GET / HTTP/1.0\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        request);

    expect(parsed, "HTTP/1.0 Connection: keep-alive request should parse");
    expect(request.keepAlive, "HTTP/1.0 Connection: keep-alive should enable keep-alive");
    expectResponseConnectionHeader(
        request.keepAlive,
        "Connection: keep-alive\r\n",
        "HTTP/1.0 Connection: keep-alive response should keep alive");
}

void testHttpParserParsesHeadRequest() {
    HttpRequest request;
    const bool parsed = HttpParser::parse(
        "HEAD /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n",
        request);

    expect(parsed, "HEAD request should parse");
    expect(request.method == "HEAD", "method should be HEAD");
    expect(request.path == "/index.html", "HEAD path should parse");
    expect(request.keepAlive, "HEAD HTTP/1.1 should keep alive by default");
}

void testHttpParserParsesContentLength() {
    HttpRequest request;
    const bool parsed = HttpParser::parse(
        "POST /api/echo HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 5\r\n"
        "\r\n",
        request);

    expect(parsed, "POST request with Content-Length should parse");
    expect(request.method == "POST", "method should be POST");
    expect(request.path == "/api/echo", "POST path should parse");
    expect(request.hasContentLength, "Content-Length presence should be recorded");
    expect(request.contentLength == 5, "Content-Length value should parse");
    expect(request.body.empty(), "parser should not consume body bytes");
}

void testHttpParserRejectsInvalidContentLength() {
    expectParseFailure(
        "POST /api/echo HTTP/1.1\r\n"
        "Content-Length: abc\r\n"
        "\r\n",
        "non-numeric Content-Length should fail");

    expectParseFailure(
        "POST /api/echo HTTP/1.1\r\n"
        "Content-Length: -1\r\n"
        "\r\n",
        "negative Content-Length should fail");

    expectParseFailure(
        "POST /api/echo HTTP/1.1\r\n"
        "Content-Length: 5\r\n"
        "Content-Length: 5\r\n"
        "\r\n",
        "duplicate Content-Length should fail");
}

void testHttpParserRejectsUnsupportedHttpVersion() {
    expectParseFailure(
        "GET / HTTP/2.0\r\n"
        "\r\n",
        "HTTP/2.0 should fail");
}

void testHttpParserRejectsPathWithoutLeadingSlash() {
    expectParseFailure(
        "GET index.html HTTP/1.1\r\n"
        "\r\n",
        "path without leading slash should fail");
}

void testHttpParserRejectsInvalidHeader() {
    expectParseFailure(
        "GET / HTTP/1.1\r\n"
        "Host localhost\r\n"
        "\r\n",
        "header without colon should fail");

    expectParseFailure(
        "GET / HTTP/1.1\r\n"
        "Bad Header: value\r\n"
        "\r\n",
        "header with invalid name should fail");
}

void testHttpParserKeepsRawUrlPath() {
    HttpRequest request;
    const bool parsed = HttpParser::parse(
        "GET /hello%20world.txt?download=1 HTTP/1.1\r\n"
        "\r\n",
        request);

    expect(parsed, "URL encoded request should parse");
    expect(
        request.path == "/hello%20world.txt?download=1",
        "parser should preserve raw request target for static handler");
}

void testHttpParserLeavesInvalidUrlEncodingForHandler() {
    HttpRequest incomplete;
    expect(
        HttpParser::parse(
            "GET /hello%2 HTTP/1.1\r\n"
            "\r\n",
            incomplete),
        "incomplete URL encoding should parse at request-line level");
    expect(incomplete.path == "/hello%2", "incomplete URL encoding should be preserved");

    HttpRequest nonHex;
    expect(
        HttpParser::parse(
            "GET /hello%GG HTTP/1.1\r\n"
            "\r\n",
            nonHex),
        "non-hex URL encoding should parse at request-line level");
    expect(nonHex.path == "/hello%GG", "non-hex URL encoding should be preserved");
}

void testHttpParserStoresHeaderNamesCaseInsensitively() {
    HttpRequest request;
    const bool parsed = HttpParser::parse(
        "GET / HTTP/1.1\r\n"
        "hOsT: localhost\r\n"
        "USER-Agent: test\r\n"
        "\r\n",
        request);

    expect(parsed, "mixed-case headers should parse");
    expect(request.headers["host"] == "localhost", "host header should be available in lowercase");
    expect(request.headers["user-agent"] == "test", "user-agent header should be available in lowercase");
    expect(request.headers.find("hOsT") == request.headers.end(), "original header casing should not be stored");
}

void testHttpParserRejectsOverlongRequestLineAndHeaders() {
    expectParseFailure(
        "GET /" + std::string(8192, 'a') + " HTTP/1.1\r\n"
        "\r\n",
        "overlong request line should fail");

    expectParseFailure(
        "GET / HTTP/1.1\r\n"
        "X-Test: " + std::string(8192, 'a') + "\r\n"
        "\r\n",
        "overlong header section should fail");
}

void testHttpResponseSerializesStatusHeadersAndBody() {
    HttpResponse response(200, "OK");
    response.setContentType("text/plain; charset=utf-8");
    response.setBody("hello");

    const std::string raw = response.toString();
    expect(raw.find("HTTP/1.1 200 OK\r\n") == 0, "status line should serialize");
    expect(
        raw.find("Content-Type: text/plain; charset=utf-8\r\n") != std::string::npos,
        "content type should serialize");
    expect(raw.find("Content-Length: 5\r\n") != std::string::npos, "content length should serialize");
    expect(response.bodySize() == 5, "body size should match serialized Content-Length");
    expect(raw.find("Connection: close\r\n") != std::string::npos, "connection close should serialize by default");
    expect(
        raw.rfind("\r\n\r\nhello") == raw.size() - std::string("\r\n\r\nhello").size(),
        "body should serialize after header separator");
}

void testHttpResponseSerializesKeepAliveConnectionHeader() {
    HttpResponse response(200, "OK");
    response.setKeepAlive(true);
    response.setBody("hello");

    const std::string raw = response.toString();
    expect(
        raw.find("Connection: keep-alive\r\n") != std::string::npos,
        "keep-alive connection header should serialize");
}

void testHttpResponseCanSerializeHeadersWithoutBody() {
    HttpResponse response(200, "OK");
    response.setContentType("text/plain; charset=utf-8");
    response.setBody("hello");

    const std::string getRaw = response.toString();
    const std::string headRaw = response.toString(false);

    expect(responseHeaders(headRaw) == responseHeaders(getRaw), "HEAD headers should match GET headers");
    expect(response.bodySize() == 5, "HEAD log body size should keep GET Content-Length");
    expect(headRaw.find("Content-Length: 5\r\n") != std::string::npos, "HEAD should keep GET content length");
    expect(responseBody(headRaw).empty(), "HEAD response should not serialize body");
    expect(responseBody(getRaw) == "hello", "GET response body should still serialize");
}

void testHttpResponseSerializesCustomHeaders() {
    HttpResponse response(206, "Partial Content");
    response.setContentType("text/plain; charset=utf-8");
    response.setContentLength(4);
    response.setHeader("Content-Range", "bytes 0-3/10");

    const std::string raw = response.toString();
    expect(raw.find("HTTP/1.1 206 Partial Content\r\n") == 0, "partial content status should serialize");
    expect(raw.find("Content-Length: 4\r\n") != std::string::npos, "range length should serialize");
    expect(raw.find("Content-Range: bytes 0-3/10\r\n") != std::string::npos, "content range should serialize");
}

void testHttpResponseSupportsJsonBody() {
    HttpResponse response(200, "OK");
    response.setJsonBody("{\"ok\":true}");

    const std::string raw = response.toString();
    expect(
        raw.find("Content-Type: application/json\r\n") != std::string::npos,
        "JSON content type should serialize");
    expect(responseBody(raw) == "{\"ok\":true}", "JSON response body should serialize");
}

void testRouterHandlesPingRoute() {
    Router router;
    HttpRequest request;
    request.method = "GET";
    request.path = "/api/ping";
    request.keepAlive = true;

    const std::optional<HttpResponse> response = router.handle(request);
    expect(response.has_value(), "router should handle /api/ping");

    const std::string raw = response->toString();
    expect(raw.find("HTTP/1.1 200 OK\r\n") == 0, "ping should return 200");
    expect(
        raw.find("Content-Type: application/json\r\n") != std::string::npos,
        "ping should return JSON");
    expect(raw.find("Connection: keep-alive\r\n") != std::string::npos, "ping should preserve keep-alive");
    expect(responseBody(raw) == "{\"message\":\"pong\"}", "ping body should be pong JSON");
}

void testRouterHandlesTimeRoute() {
    Router router;
    HttpRequest request;
    request.method = "GET";
    request.path = "/api/time";

    const std::optional<HttpResponse> response = router.handle(request);
    expect(response.has_value(), "router should handle /api/time");

    const std::string raw = response->toString();
    expect(raw.find("HTTP/1.1 200 OK\r\n") == 0, "time should return 200");
    expect(responseBody(raw).find("{\"time\":\"") == 0, "time should return a JSON time field");
}

void testRouterEchoesRequestBody() {
    Router router;
    HttpRequest request;
    request.method = "POST";
    request.path = "/api/echo";
    request.headers["content-type"] = "application/json";
    request.body = "{\"hello\":\"world\"}";

    const std::optional<HttpResponse> response = router.handle(request);
    expect(response.has_value(), "router should handle /api/echo");

    const std::string raw = response->toString();
    expect(raw.find("HTTP/1.1 200 OK\r\n") == 0, "echo should return 200");
    expect(
        raw.find("Content-Type: application/json\r\n") != std::string::npos,
        "echo should preserve request content type");
    expect(responseBody(raw) == request.body, "echo should return request body");
}

void testRouterReturnsNotFoundForUnknownApiRoute() {
    Router router;
    HttpRequest request;
    request.method = "GET";
    request.path = "/api/missing";

    const std::optional<HttpResponse> response = router.handle(request);
    expect(response.has_value(), "router should handle unknown /api paths");

    const std::string raw = response->toString();
    expect(raw.find("HTTP/1.1 404 Not Found\r\n") == 0, "unknown API route should return 404");
    expect(responseBody(raw) == "{\"error\":\"not found\"}", "unknown API route should return JSON error");
}

void testRouterIgnoresNonApiPaths() {
    Router router;
    HttpRequest request;
    request.method = "GET";
    request.path = "/index.html";

    expect(!router.handle(request).has_value(), "router should let non-API paths fall through to static files");
}

void testRouterMatchesMethodAndPathForRegisteredRoutes() {
    Router router;
    router.addRoute("PUT", "/api/custom", [](const HttpRequest&) {
        HttpResponse response(201, "Created");
        response.setJsonBody("{\"created\":true}");
        return response;
    });

    HttpRequest request;
    request.method = "PUT";
    request.path = "/api/custom?trace=1";

    const std::optional<HttpResponse> response = router.handle(request);
    expect(response.has_value(), "custom route should be handled");

    const std::string raw = response->toString();
    expect(raw.find("HTTP/1.1 201 Created\r\n") == 0, "custom route should use registered handler");
    expect(responseBody(raw) == "{\"created\":true}", "custom route should return registered body");
}

void testWebSocketHandshakeComputesAcceptKey() {
    const std::string accept = WebSocketHandshake::acceptForKey("dGhlIHNhbXBsZSBub25jZQ==");
    expect(
        accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
        "WebSocket accept key should match RFC example");
}

void testWebSocketHandshakeAcceptsValidRequest() {
    HttpRequest request;
    const bool parsed = HttpParser::parse(
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: keep-alive, Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        request);

    expect(parsed, "valid WebSocket handshake request should parse");
    expect(WebSocketHandshake::isEndpoint(request), "WebSocket endpoint should be recognized");

    const std::optional<std::string> accept = WebSocketHandshake::acceptForRequest(request);
    expect(accept.has_value(), "valid WebSocket handshake should produce accept key");
    expect(*accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "valid WebSocket accept key should match");
}

void testWebSocketHandshakeRejectsInvalidRequest() {
    HttpRequest missingUpgrade;
    expect(
        HttpParser::parse(
            "GET /ws HTTP/1.1\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n",
            missingUpgrade),
        "missing upgrade fixture should parse");
    expect(
        !WebSocketHandshake::acceptForRequest(missingUpgrade).has_value(),
        "missing Upgrade header should reject WebSocket handshake");

    HttpRequest wrongVersion;
    expect(
        HttpParser::parse(
            "GET /ws HTTP/1.1\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 12\r\n"
            "\r\n",
            wrongVersion),
        "wrong version fixture should parse");
    expect(
        !WebSocketHandshake::acceptForRequest(wrongVersion).has_value(),
        "Sec-WebSocket-Version other than 13 should reject handshake");

    HttpRequest shortKey;
    expect(
        HttpParser::parse(
            "GET /ws HTTP/1.1\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: c2hvcnQ=\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n",
            shortKey),
        "short key fixture should parse");
    expect(
        !WebSocketHandshake::acceptForRequest(shortKey).has_value(),
        "Sec-WebSocket-Key must decode to 16 bytes");
}

void testWebSocketHandshakeSerializesSwitchingProtocolsResponse() {
    const std::string raw =
        WebSocketHandshake::switchingProtocolsResponse("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    expect(
        raw ==
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
            "\r\n",
        "WebSocket 101 response should contain only upgrade headers");
}

void testStaticFileHandlerServesIndexForRoot() {
    StaticFileHandler handler(WEBSERVER_TEST_WWW_DIR);

    const StaticFileResult index = handler.handle("/");
    expect(index.status == StaticFileResult::Status::Ok, "index should be served");
    expect(index.contentType == "text/html; charset=utf-8", "index content type should be HTML");
    expect(!index.filePath.empty(), "index file path should be recorded");
    expect(index.fileSize > 0, "index file size should be recorded");
    expect(index.dynamicBody, "small index should be served from cache");
    expect(index.body.size() == index.fileSize, "cached index body should match file size");
}

void testStaticFileHandlerServesIndexBeforeDirectoryListing() {
    TemporaryDirectory root;
    std::filesystem::create_directories(root.path() / "docs");
    writeFile(root.path() / "docs" / "index.html", "docs index");

    StaticFileHandler handler(root.path().string(), true);
    const StaticFileResult result = handler.handle("/docs/");

    expect(result.status == StaticFileResult::Status::Ok, "directory index.html should be served first");
    expect(result.dynamicBody, "small index.html should be served from cache");
    expect(result.body == "docs index", "cached directory index should include file body");
    expect(result.filePath.filename() == "index.html", "directory should resolve to index.html");
    expect(result.fileSize == 10, "index.html file size should be recorded");
}

void testStaticFileHandlerBuildsDirectoryListingWhenIndexMissing() {
    TemporaryDirectory root;
    std::filesystem::create_directories(root.path() / "docs" / "assets");
    writeFile(root.path() / "docs" / "read me.txt", "hello");

    StaticFileHandler handler(root.path().string(), true);
    const StaticFileResult result = handler.handle("/docs/");

    expect(result.status == StaticFileResult::Status::Ok, "directory without index should return listing");
    expect(result.dynamicBody, "directory listing should be a dynamic body");
    expect(result.contentType == "text/html; charset=utf-8", "directory listing should be HTML");
    expect(result.contentLength == result.body.size(), "directory listing length should match body");
    expect(result.body.find("read me.txt") != std::string::npos, "listing should include file name");
    expect(result.body.find("assets/") != std::string::npos, "listing should include directory name");
    expect(result.body.find("href=\"/docs/read%20me.txt\"") != std::string::npos, "file link should be URL encoded");
    expect(result.body.find("<td>yes</td>") != std::string::npos, "listing should show directory flag");
    expect(result.body.find("<td>5</td>") != std::string::npos, "listing should show file size");
}

void testStaticFileHandlerCanDisableDirectoryListing() {
    TemporaryDirectory root;
    std::filesystem::create_directories(root.path() / "docs");

    StaticFileHandler handler(root.path().string(), false);
    const StaticFileResult result = handler.handle("/docs/");

    expect(result.status == StaticFileResult::Status::Forbidden, "disabled directory listing should return 403");
}

void testStaticFileHandlerIgnoresRangeForDirectoryListing() {
    TemporaryDirectory root;
    std::filesystem::create_directories(root.path() / "docs");
    writeFile(root.path() / "docs" / "file.txt", "hello");

    StaticFileHandler handler(root.path().string(), true);
    const StaticFileResult result = handler.handle("/docs/", std::string("bytes=0-3"));

    expect(result.status == StaticFileResult::Status::Ok, "directory listing should ignore Range");
    expect(result.dynamicBody, "range on directory listing should still produce dynamic listing");
    expect(!result.partialContent, "directory listing Range should not return partial content metadata");
    expect(result.contentOffset == 0, "directory listing should not use range offset");
    expect(result.contentLength == result.body.size(), "directory listing should keep full body length");
}

void testHeadDirectoryListingUsesHeadersWithoutBody() {
    TemporaryDirectory root;
    std::filesystem::create_directories(root.path() / "docs");
    writeFile(root.path() / "docs" / "file.txt", "hello");

    StaticFileHandler handler(root.path().string(), true);
    const StaticFileResult listing = handler.handle("/docs/");
    expect(listing.status == StaticFileResult::Status::Ok, "HEAD directory listing target should resolve");
    expect(listing.dynamicBody, "HEAD directory listing fixture should be dynamic");

    HttpResponse response(200, "OK");
    response.setContentType(listing.contentType);
    response.setBody(listing.body);

    const std::string headRaw = response.toString(false);
    expect(headRaw.find("HTTP/1.1 200 OK\r\n") == 0, "HEAD directory listing should return 200");
    expect(
        headRaw.find("Content-Length: " + std::to_string(listing.body.size()) + "\r\n") != std::string::npos,
        "HEAD directory listing should keep generated body length");
    expect(responseBody(headRaw).empty(), "HEAD directory listing should not include body");
}

void testStaticFileHandlerSkipsDirectoryEntriesOutsideRoot() {
    TemporaryDirectory root;
    TemporaryDirectory outside;
    std::filesystem::create_directories(root.path() / "docs");
    writeFile(root.path() / "docs" / "inside.txt", "inside");
    writeFile(outside.path() / "secret.txt", "secret");

    std::error_code error;
    std::filesystem::create_directory_symlink(outside.path(), root.path() / "docs" / "outside", error);
    if (error) {
        return;
    }

    StaticFileHandler handler(root.path().string(), true);
    const StaticFileResult result = handler.handle("/docs/");

    expect(result.status == StaticFileResult::Status::Ok, "directory listing with symlink should return 200");
    expect(result.body.find("inside.txt") != std::string::npos, "listing should include in-root entry");
    expect(result.body.find("outside/") == std::string::npos, "listing should skip symlinked directory outside root");
}

void testStaticFileHandlerReturnsNotFoundForMissingFile() {
    StaticFileHandler handler(WEBSERVER_TEST_WWW_DIR);

    const StaticFileResult missing = handler.handle("/missing-file.txt");
    expect(missing.status == StaticFileResult::Status::NotFound, "missing file should return 404");
}

void testStaticFileHandlerServesPlainPath() {
    TemporaryDirectory root;
    writeFile(root.path() / "hello.txt", "plain");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle("/hello.txt");

    expect(result.status == StaticFileResult::Status::Ok, "plain path should be served");
    expect(result.filePath.filename() == "hello.txt", "plain path file name should match fixture");
    expect(result.fileSize == 5, "plain path file size should match fixture");
    expect(result.contentLength == 5, "plain path content length should match fixture");
    expect(result.dynamicBody, "small plain path should be served from cache");
    expect(result.body == "plain", "plain path cached body should match fixture");
}

void testStaticFileHandlerGzipsSmallTextWhenAccepted() {
    TemporaryDirectory root;
    const std::string script = "const value = 42;\n" + std::string(256, 'x');
    writeFile(root.path() / "app.js", script);

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle(
        "/app.js",
        std::nullopt,
        std::nullopt,
        std::string("br, gzip;q=1.0"));

    expect(result.status == StaticFileResult::Status::Ok, "gzip target should be served");
    expect(result.gzipEncoded, "small JavaScript should be gzip encoded when accepted");
    expect(result.dynamicBody, "gzip response should be returned from memory");
    expect(result.contentType == "application/javascript; charset=utf-8", "gzip should keep content type");
    expect(result.contentLength == result.body.size(), "gzip content length should be compressed body length");
    expect(result.body.size() >= 2, "gzip body should not be empty");
    expect(
        static_cast<unsigned char>(result.body[0]) == 0x1f &&
            static_cast<unsigned char>(result.body[1]) == 0x8b,
        "gzip body should include gzip magic bytes");
    expect(gunzip(result.body) == script, "gzip body should decompress to original content");
}

void testStaticFileHandlerSkipsGzipWhenQualityIsZero() {
    TemporaryDirectory root;
    writeFile(root.path() / "style.css", "body { color: red; }\n");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle(
        "/style.css",
        std::nullopt,
        std::nullopt,
        std::string("gzip;q=0, *;q=1"));

    expect(result.status == StaticFileResult::Status::Ok, "q=0 gzip target should be served");
    expect(!result.gzipEncoded, "explicit gzip q=0 should disable gzip");
    expect(result.body == "body { color: red; }\n", "q=0 gzip body should stay uncompressed");
}

void testStaticFileHandlerSkipsGzipForBinaryContent() {
    TemporaryDirectory root;
    writeFile(root.path() / "image.png", "not really a png");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle(
        "/image.png",
        std::nullopt,
        std::nullopt,
        std::string("gzip"));

    expect(result.status == StaticFileResult::Status::Ok, "binary target should be served");
    expect(!result.gzipEncoded, "image/png should not be gzip encoded");
    expect(result.body == "not really a png", "binary body should stay uncompressed");
}

void testStaticFileHandlerSkipsGzipForRangeRequest() {
    TemporaryDirectory root;
    writeFile(root.path() / "range.txt", "0123456789");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle(
        "/range.txt",
        std::string("bytes=0-3"),
        std::nullopt,
        std::string("gzip"));

    expect(result.status == StaticFileResult::Status::Ok, "range gzip target should be served");
    expect(result.partialContent, "range request should keep partial content metadata");
    expect(!result.gzipEncoded, "range request should not be gzip encoded");
    expect(result.body == "0123", "range request should keep sliced body");
}

void testStaticFileHandlerDecodesUrlEncodedSpace() {
    TemporaryDirectory root;
    writeFile(root.path() / "hello world.txt", "space");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle("/hello%20world.txt");

    expect(result.status == StaticFileResult::Status::Ok, "URL encoded space path should be served");
    expect(result.filePath.filename() == "hello world.txt", "URL decoded path file name should match fixture");
    expect(result.fileSize == 5, "URL decoded path file size should match fixture");
}

void testStaticFileHandlerRejectsInvalidUrlEncoding() {
    StaticFileHandler handler(WEBSERVER_TEST_WWW_DIR);

    const StaticFileResult incomplete = handler.handle("/hello%2");
    expect(
        incomplete.status == StaticFileResult::Status::BadRequest,
        "incomplete percent encoding should be rejected");

    const StaticFileResult nonHex = handler.handle("/hello%GG");
    expect(nonHex.status == StaticFileResult::Status::BadRequest, "non-hex percent encoding should be rejected");
}

void testStaticFileHandlerIgnoresQueryString() {
    TemporaryDirectory root;
    writeFile(root.path() / "hello.txt", "query");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle("/hello.txt?download=1#section");

    expect(result.status == StaticFileResult::Status::Ok, "query string and fragment should be ignored");
    expect(result.filePath.filename() == "hello.txt", "query string target should resolve to the underlying file");
    expect(result.fileSize == 5, "query string target file size should match fixture");
}

void testStaticFileHandlerRejectsTraversalPaths() {
    StaticFileHandler handler(WEBSERVER_TEST_WWW_DIR);

    const StaticFileResult traversal = handler.handle("/../config/server.conf");
    expect(traversal.status == StaticFileResult::Status::BadRequest, ".. path traversal should be rejected");

    const StaticFileResult encodedTraversal = handler.handle("/%2e%2e/config/server.conf");
    expect(
        encodedTraversal.status == StaticFileResult::Status::BadRequest,
        "encoded .. path traversal should be rejected");

    const StaticFileResult backslash = handler.handle("/dir\\file.txt");
    expect(backslash.status == StaticFileResult::Status::BadRequest, "backslash path should be rejected");

    std::string nullPath = "/bad";
    nullPath.push_back('\0');
    nullPath += "file.txt";
    const StaticFileResult nullByte = handler.handle(nullPath);
    expect(nullByte.status == StaticFileResult::Status::BadRequest, "null byte path should be rejected");
}

void testStaticFileHandlerMimeTypes() {
    TemporaryDirectory root;
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"/index.html", "text/html; charset=utf-8"},
        {"/style.css", "text/css; charset=utf-8"},
        {"/app.js", "application/javascript; charset=utf-8"},
        {"/note.txt", "text/plain; charset=utf-8"},
        {"/image.png", "image/png"},
        {"/photo.jpg", "image/jpeg"},
        {"/photo.jpeg", "image/jpeg"},
        {"/animation.gif", "image/gif"},
        {"/icon.svg", "image/svg+xml"},
        {"/favicon.ico", "image/x-icon"},
        {"/data.json", "application/json"},
        {"/document.pdf", "application/pdf"},
        {"/module.wasm", "application/wasm"},
    };

    for (const auto& mimeCase : cases) {
        writeFile(root.path() / mimeCase.first.substr(1), "body");
    }

    StaticFileHandler handler(root.path().string());
    for (const auto& mimeCase : cases) {
        const StaticFileResult result = handler.handle(mimeCase.first);
        expect(result.status == StaticFileResult::Status::Ok, "MIME fixture should be served");
        expect(result.contentType == mimeCase.second, "MIME type should match extension");
    }
}

void testStaticFileHandlerParsesExplicitByteRange() {
    TemporaryDirectory root;
    writeFile(root.path() / "range.txt", "0123456789");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle("/range.txt", std::string("bytes=0-3"));

    expect(result.status == StaticFileResult::Status::Ok, "bytes=0-3 should be satisfiable");
    expect(result.partialContent, "explicit byte range should mark partial content");
    expect(result.fileSize == 10, "range total size should match file size");
    expect(result.contentOffset == 0, "bytes=0-3 should start at offset 0");
    expect(result.contentLength == 4, "bytes=0-3 should have length 4");
    expect(result.body == "0123", "small range should be sliced from cached body");
}

void testStaticFileHandlerParsesOpenEndedByteRange() {
    TemporaryDirectory root;
    writeFile(root.path() / "range.txt", "0123456789");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle("/range.txt", std::string("bytes=3-"));

    expect(result.status == StaticFileResult::Status::Ok, "bytes=3- should be satisfiable");
    expect(result.partialContent, "open ended range should mark partial content");
    expect(result.contentOffset == 3, "bytes=3- should start at offset 3");
    expect(result.contentLength == 7, "bytes=3- should continue through EOF");
    expect(result.body == "3456789", "small open-ended range should be sliced from cached body");
}

void testStaticFileHandlerParsesSuffixByteRange() {
    TemporaryDirectory root;
    writeFile(root.path() / "range.txt", "0123456789");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle("/range.txt", std::string("bytes=-4"));

    expect(result.status == StaticFileResult::Status::Ok, "bytes=-4 should be satisfiable");
    expect(result.partialContent, "suffix range should mark partial content");
    expect(result.contentOffset == 6, "bytes=-4 should start four bytes before EOF");
    expect(result.contentLength == 4, "bytes=-4 should have length 4");
    expect(result.body == "6789", "small suffix range should be sliced from cached body");
}

void testStaticFileHandlerKeepsLargeFilesOnSendfilePath() {
    TemporaryDirectory root;
    writeFile(root.path() / "large.bin", std::string(70 * 1024, 'x'));

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle("/large.bin");

    expect(result.status == StaticFileResult::Status::Ok, "large file should be served");
    expect(!result.dynamicBody, "large file should not be loaded into cache");
    expect(result.body.empty(), "large file body should be left for sendfile");
    expect(result.contentLength == result.fileSize, "large file content length should match file size");
}

void testStaticFileHandlerKeepsLargeCompressibleFilesOnSendfilePath() {
    TemporaryDirectory root;
    writeFile(root.path() / "large.txt", std::string(70 * 1024, 'x'));

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle(
        "/large.txt",
        std::nullopt,
        std::nullopt,
        std::string("gzip"));

    expect(result.status == StaticFileResult::Status::Ok, "large text file should be served");
    expect(!result.dynamicBody, "large text file should stay on sendfile path");
    expect(!result.gzipEncoded, "large text file should not be gzip encoded yet");
    expect(result.body.empty(), "large text body should be left for sendfile");
    expect(result.contentLength == result.fileSize, "large text content length should match file size");
}

void testStaticFileHandlerKeepsLargeRangeOnSendfilePath() {
    TemporaryDirectory root;
    writeFile(root.path() / "large.bin", std::string(70 * 1024, 'x'));

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle("/large.bin", std::string("bytes=10-19"));

    expect(result.status == StaticFileResult::Status::Ok, "large range should be satisfiable");
    expect(result.partialContent, "large range should mark partial content");
    expect(!result.dynamicBody, "large range should stay on sendfile path");
    expect(result.body.empty(), "large range body should be left for sendfile");
    expect(result.contentOffset == 10, "large range offset should be recorded");
    expect(result.contentLength == 10, "large range length should be recorded");
}

void testStaticFileHandlerReloadsCachedFileWhenModified() {
    TemporaryDirectory root;
    const std::filesystem::path filePath = root.path() / "reload.txt";
    writeFile(filePath, "first");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult first = handler.handle("/reload.txt");
    expect(first.status == StaticFileResult::Status::Ok, "initial cached file should be served");
    expect(first.body == "first", "initial cached body should match fixture");

    const std::filesystem::file_time_type originalModified = std::filesystem::last_write_time(filePath);
    writeFile(filePath, "again");
    std::filesystem::last_write_time(filePath, originalModified + std::chrono::seconds(2));

    const StaticFileResult second = handler.handle("/reload.txt");
    expect(second.status == StaticFileResult::Status::Ok, "modified cached file should be served");
    expect(second.body == "again", "modified cached file should be reloaded");
}

void testStaticFileHandlerAddsStableEtag() {
    TemporaryDirectory root;
    writeFile(root.path() / "etag.txt", "etag-body");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult first = handler.handle("/etag.txt");
    const StaticFileResult second = handler.handle("/etag.txt");

    expect(first.status == StaticFileResult::Status::Ok, "ETag fixture should be served");
    expect(!first.etag.empty(), "static file should include an ETag");
    expect(first.etag.front() == '"' && first.etag.back() == '"', "ETag should be quoted");
    expect(second.etag == first.etag, "unchanged file should keep the same ETag");
}

void testStaticFileHandlerIfNoneMatchReturnsNotModified() {
    TemporaryDirectory root;
    writeFile(root.path() / "etag.txt", "etag-body");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult original = handler.handle("/etag.txt");
    expect(original.status == StaticFileResult::Status::Ok, "original ETag fixture should be served");

    const StaticFileResult cached = handler.handle("/etag.txt", std::nullopt, original.etag);

    expect(
        cached.status == StaticFileResult::Status::NotModified,
        "matching If-None-Match should return not modified");
    expect(cached.etag == original.etag, "304 should keep matching ETag");
    expect(cached.body.empty(), "304 should not include a body");
    expect(cached.contentLength == 0, "304 content length metadata should be zero");
}

void testStaticFileHandlerIfNoneMatchSupportsListsAndWeakEtags() {
    TemporaryDirectory root;
    writeFile(root.path() / "etag.txt", "etag-body");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult original = handler.handle("/etag.txt");
    expect(original.status == StaticFileResult::Status::Ok, "original ETag fixture should be served");

    const StaticFileResult listed = handler.handle(
        "/etag.txt",
        std::nullopt,
        "\"missing\", W/" + original.etag);

    expect(
        listed.status == StaticFileResult::Status::NotModified,
        "If-None-Match should support lists and weak ETags");
}

void testStaticFileHandlerIfNoneMatchTakesPriorityOverRange() {
    TemporaryDirectory root;
    writeFile(root.path() / "range-etag.txt", "0123456789");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult original = handler.handle("/range-etag.txt");
    expect(original.status == StaticFileResult::Status::Ok, "original range ETag fixture should be served");

    const StaticFileResult cached = handler.handle(
        "/range-etag.txt",
        std::string("bytes=0-3"),
        original.etag);

    expect(
        cached.status == StaticFileResult::Status::NotModified,
        "matching If-None-Match should return 304 before Range handling");
    expect(!cached.partialContent, "304 should not be marked as partial content");
    expect(cached.body.empty(), "304 from range request should not include a body");
}

void testStaticFileHandlerRangeRunsWhenIfNoneMatchMisses() {
    TemporaryDirectory root;
    writeFile(root.path() / "range-etag.txt", "0123456789");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle(
        "/range-etag.txt",
        std::string("bytes=0-3"),
        std::string("\"different\""));

    expect(result.status == StaticFileResult::Status::Ok, "ETag miss should continue to Range handling");
    expect(result.partialContent, "ETag miss with Range should return partial content");
    expect(result.contentLength == 4, "ETag miss range should keep requested length");
    expect(result.body == "0123", "ETag miss range should return sliced body");
}

void testFileCacheEvictsLeastRecentlyUsedEntry() {
    TemporaryDirectory root;
    const std::filesystem::path firstPath = root.path() / "first.txt";
    const std::filesystem::path secondPath = root.path() / "second.txt";
    writeFile(firstPath, "aaaaa");
    writeFile(secondPath, "bbbbb");

    FileCache cache(64, 8);
    const std::filesystem::file_time_type firstModified = std::filesystem::file_time_type::clock::now();
    const std::filesystem::file_time_type secondModified = firstModified + std::chrono::seconds(1);

    const std::optional<FileCache::Entry> first =
        cache.get(firstPath, 5, firstModified, "text/plain; charset=utf-8");
    expect(first.has_value() && first->content == "aaaaa", "first cache load should succeed");

    const std::optional<FileCache::Entry> second =
        cache.get(secondPath, 5, secondModified, "text/plain; charset=utf-8");
    expect(second.has_value() && second->content == "bbbbb", "second cache load should succeed");
    expect(cache.currentSize() == 5, "cache should evict one entry when capacity is exceeded");

    writeFile(firstPath, "ccccc");
    const std::optional<FileCache::Entry> reloaded =
        cache.get(firstPath, 5, firstModified, "text/plain; charset=utf-8");
    expect(reloaded.has_value() && reloaded->content == "ccccc", "LRU entry should be reloaded after eviction");
}

void testStaticFileHandlerRejectsUnsatisfiableByteRange() {
    TemporaryDirectory root;
    writeFile(root.path() / "range.txt", "0123456789");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle("/range.txt", std::string("bytes=20-30"));

    expect(
        result.status == StaticFileResult::Status::RangeNotSatisfiable,
        "out of bounds range should return range not satisfiable");
    expect(result.fileSize == 10, "416 result should keep total file size");
}

void testStaticFileHandlerRejectsInvalidByteRangeFormat() {
    TemporaryDirectory root;
    writeFile(root.path() / "range.txt", "0123456789");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult result = handler.handle("/range.txt", std::string("bytes=bad"));

    expect(
        result.status == StaticFileResult::Status::RangeNotSatisfiable,
        "invalid range format should return range not satisfiable");
    expect(result.fileSize == 10, "invalid range result should keep total file size");
}

void testHeadStaticFileRangeUsesPartialHeadersWithoutBody() {
    TemporaryDirectory root;
    writeFile(root.path() / "range.txt", "0123456789");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult file = handler.handle("/range.txt", std::string("bytes=0-3"));
    expect(file.status == StaticFileResult::Status::Ok, "HEAD range target should be satisfiable");

    HttpResponse response(206, "Partial Content");
    response.setContentType(file.contentType);
    response.setContentLength(static_cast<std::size_t>(file.contentLength));
    response.setHeader(
        "Content-Range",
        "bytes " + std::to_string(file.contentOffset) + "-" +
            std::to_string(file.contentOffset + file.contentLength - 1) + "/" +
            std::to_string(file.fileSize));

    const std::string headRaw = response.toString(false);

    expect(headRaw.find("HTTP/1.1 206 Partial Content\r\n") == 0, "HEAD range should return 206");
    expect(headRaw.find("Content-Length: 4\r\n") != std::string::npos, "HEAD range should keep range length");
    expect(
        headRaw.find("Content-Range: bytes 0-3/10\r\n") != std::string::npos,
        "HEAD range should include Content-Range");
    expect(responseBody(headRaw).empty(), "HEAD range response should not include body");
}

void testHeadStaticFileSuccessUsesGetHeadersWithoutBody() {
    TemporaryDirectory root;
    writeFile(root.path() / "index.html", "hello");

    StaticFileHandler handler(root.path().string());
    const StaticFileResult file = handler.handle("/index.html");
    expect(file.status == StaticFileResult::Status::Ok, "HEAD target should be found through static handler");

    HttpResponse response(200, "OK");
    response.setContentType(file.contentType);
    response.setContentLength(static_cast<std::size_t>(file.fileSize));

    const std::string headRaw = response.toString(false);

    expect(headRaw.find("HTTP/1.1 200 OK\r\n") == 0, "HEAD static file should return 200");
    expect(headRaw.find("Content-Length: 5\r\n") != std::string::npos, "HEAD static file should keep file size");
    expect(responseBody(headRaw).empty(), "HEAD static file response should not include body");
}

void testHeadStaticFileNotFoundHasHeadersWithoutBody() {
    StaticFileHandler handler(WEBSERVER_TEST_WWW_DIR);
    const StaticFileResult missing = handler.handle("/missing-file.txt");
    expect(missing.status == StaticFileResult::Status::NotFound, "HEAD missing target should return not found");

    HttpResponse response(404, "Not Found");
    response.setBody("Not Found");

    const std::string headRaw = response.toString(false);
    expect(headRaw.find("HTTP/1.1 404 Not Found\r\n") == 0, "HEAD missing file should return 404");
    expect(headRaw.find("Content-Length: 9\r\n") != std::string::npos, "HEAD 404 should keep error body length");
    expect(responseBody(headRaw).empty(), "HEAD 404 response should not include body");
}

void testConfigParsesConnectionIdleTimeout() {
    TemporaryDirectory root;
    const std::filesystem::path configPath = root.path() / "server.conf";
    writeFile(
        configPath,
        "port=9090\n"
        "thread_num=2\n"
        "root=www\n"
        "connection_idle_timeout_seconds=5\n"
        "max_request_body_size=2048\n"
        "enable_directory_listing=false\n"
        "enable_tls=true\n"
        "cert_file=test-cert.pem\n"
        "key_file=test-key.pem\n");

    Config config(configPath.string());

    expect(config.port() == 9090, "config should parse port");
    expect(config.threadNum() == 2, "config should parse thread count");
    expect(config.root() == "www", "config should parse root");
    expect(
        config.connectionIdleTimeout() == std::chrono::seconds(5),
        "config should parse connection idle timeout");
    expect(config.maxRequestBodySize() == 2048, "config should parse max request body size");
    expect(!config.enableDirectoryListing(), "config should parse directory listing flag");
    expect(config.enableTls(), "config should parse TLS flag");
    expect(config.certFile() == "test-cert.pem", "config should parse TLS certificate path");
    expect(config.keyFile() == "test-key.pem", "config should parse TLS private key path");
}

void testConfigDefaultsRequestLimits() {
    TemporaryDirectory root;
    Config config((root.path() / "missing.conf").string());

    expect(
        config.connectionIdleTimeout() == std::chrono::seconds(30),
        "missing config should use default connection idle timeout");
    expect(
        config.maxRequestBodySize() == 1024 * 1024,
        "missing config should use default max request body size");
    expect(config.enableDirectoryListing(), "missing config should use default directory listing flag");
    expect(!config.enableTls(), "missing config should disable TLS by default");
    expect(config.certFile() == "cert.pem", "missing config should use default certificate path");
    expect(config.keyFile() == "key.pem", "missing config should use default private key path");
}

void testConfigRejectsInvalidTlsFlag() {
    TemporaryDirectory root;
    const std::filesystem::path configPath = root.path() / "server.conf";
    writeFile(
        configPath,
        "port=9090\n"
        "thread_num=2\n"
        "root=www\n"
        "connection_idle_timeout_seconds=5\n"
        "max_request_body_size=2048\n"
        "enable_tls=maybe\n");

    Config config(configPath.string());

    expect(config.port() == 8080, "invalid TLS flag should reset config to default port");
    expect(!config.enableTls(), "invalid TLS flag should reset TLS to default");
}

void testConfigRejectsInvalidMaxRequestBodySize() {
    TemporaryDirectory root;
    const std::filesystem::path configPath = root.path() / "server.conf";
    writeFile(
        configPath,
        "port=9090\n"
        "thread_num=2\n"
        "root=www\n"
        "connection_idle_timeout_seconds=5\n"
        "max_request_body_size=0\n");

    Config config(configPath.string());

    expect(config.port() == 8080, "invalid max body size should reset config to default port");
    expect(
        config.maxRequestBodySize() == 1024 * 1024,
        "invalid max body size should reset to default max request body size");
}

void testConfigRejectsInvalidDirectoryListingFlag() {
    TemporaryDirectory root;
    const std::filesystem::path configPath = root.path() / "server.conf";
    writeFile(
        configPath,
        "port=9090\n"
        "thread_num=2\n"
        "root=www\n"
        "connection_idle_timeout_seconds=5\n"
        "max_request_body_size=2048\n"
        "enable_directory_listing=maybe\n");

    Config config(configPath.string());

    expect(config.port() == 8080, "invalid directory listing flag should reset config to default port");
    expect(config.enableDirectoryListing(), "invalid directory listing flag should reset to default");
}

void testCommandLineUsesDefaultConfigPath() {
    CommandLineOptions options;
    std::string errorMessage;

    expect(parseArguments({"WebServer"}, options, errorMessage), "default arguments should parse");
    expect(options.configPath == "config/server.conf", "default config path should be config/server.conf");
    expect(!options.port.has_value(), "default arguments should not override port");
    expect(!options.showHelp, "default arguments should not show help");
}

void testCommandLineOverridesPort() {
    TemporaryDirectory root;
    const std::filesystem::path configPath = root.path() / "server.conf";
    writeFile(
        configPath,
        "port=8081\n"
        "thread_num=2\n"
        "root=www\n"
        "connection_idle_timeout_seconds=5\n"
        "max_request_body_size=2048\n");

    CommandLineOptions options;
    std::string errorMessage;
    expect(
        parseArguments({"WebServer", "--config", configPath.string(), "--port", "9090"}, options, errorMessage),
        "port override arguments should parse");

    Config config(options.configPath);
    applyCommandLineOverridesToConfig(options, config);

    expect(config.port() == 9090, "command line port should override config file port");
    expect(config.threadNum() == 2, "non-overridden thread count should come from config file");
    expect(config.maxRequestBodySize() == 2048, "non-overridden max body size should come from config file");
}

void testCommandLineOverridesMaxRequestBodySize() {
    TemporaryDirectory root;
    const std::filesystem::path configPath = root.path() / "server.conf";
    writeFile(
        configPath,
        "port=8081\n"
        "thread_num=2\n"
        "root=www\n"
        "connection_idle_timeout_seconds=5\n"
        "max_request_body_size=2048\n");

    CommandLineOptions options;
    std::string errorMessage;
    expect(
        parseArguments(
            {"WebServer", "--config", configPath.string(), "--max-body-size", "4096"},
            options,
            errorMessage),
        "max body size override arguments should parse");

    Config config(options.configPath);
    applyCommandLineOverridesToConfig(options, config);

    expect(config.maxRequestBodySize() == 4096, "command line max body size should override config file");
}

void testCommandLineRejectsInvalidPort() {
    CommandLineOptions options;
    std::string errorMessage;

    expect(!parseArguments({"WebServer", "--port", "70000"}, options, errorMessage), "invalid port should fail");
    expect(errorMessage.find("--port") != std::string::npos, "invalid port error should name --port");
    expect(errorMessage.find("1-65535") != std::string::npos, "invalid port error should mention valid range");
}

void testCommandLineRejectsInvalidMaxRequestBodySize() {
    CommandLineOptions options;
    std::string errorMessage;

    expect(
        !parseArguments({"WebServer", "--max-body-size", "0"}, options, errorMessage),
        "zero max body size should fail");
    expect(
        errorMessage.find("--max-body-size") != std::string::npos,
        "invalid max body size error should name --max-body-size");
    expect(
        errorMessage.find("positive integer bytes") != std::string::npos,
        "invalid max body size error should mention expected bytes");
}

void testCommandLineHelpOutput() {
    CommandLineOptions options;
    std::string errorMessage;

    expect(parseArguments({"WebServer", "--help"}, options, errorMessage), "help argument should parse");
    expect(options.showHelp, "help argument should set showHelp");

    const std::string help = commandLineHelp("WebServer");
    expect(help.find("Usage: WebServer [options]") != std::string::npos, "help should include usage");
    expect(help.find("--config <path>") != std::string::npos, "help should include --config");
    expect(help.find("--port <port>") != std::string::npos, "help should include --port");
    expect(help.find("--max-body-size <bytes>") != std::string::npos, "help should include --max-body-size");
    expect(help.find("--help") != std::string::npos, "help should include --help");
}

void testThreadPoolExecutesTask() {
    ThreadPool pool(1, 4);
    std::promise<void> completedPromise;
    std::future<void> completed = completedPromise.get_future();

    const bool enqueued = pool.enqueue([&completedPromise]() {
        completedPromise.set_value();
    });

    expect(enqueued, "task should be enqueued");
    expect(
        completed.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
        "task should execute");
}

void testThreadPoolCreatesWorkerWhenThreadCountIsZero() {
    ThreadPool pool(0, 4);
    std::promise<void> completedPromise;
    std::future<void> completed = completedPromise.get_future();

    const bool enqueued = pool.enqueue([&completedPromise]() {
        completedPromise.set_value();
    });

    expect(enqueued, "task should be enqueued when threadCount is zero");
    expect(
        completed.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
        "threadCount zero should still create a worker");
}

void testThreadPoolRejectsTaskWhenQueueIsFull() {
    ThreadPool pool(1, 1);
    std::promise<void> startedPromise;
    std::future<void> started = startedPromise.get_future();
    std::promise<void> releasePromise;
    std::shared_future<void> release = releasePromise.get_future().share();

    const bool firstEnqueued = pool.enqueue([&startedPromise, release]() {
        startedPromise.set_value();
        release.wait();
    });

    expect(firstEnqueued, "blocking task should be enqueued");
    expect(
        started.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
        "blocking task should start");

    expect(pool.enqueue([]() {}), "one queued task should fit");
    expect(!pool.enqueue([]() {}), "enqueue should return false when queue is full");

    releasePromise.set_value();
}

void testThreadPoolDestructorJoinsWorkers() {
    std::atomic<int> completedTasks{0};
    {
        ThreadPool pool(2, 4);
        expect(pool.enqueue([&completedTasks]() {
            ++completedTasks;
        }), "first destructor test task should enqueue");
        expect(pool.enqueue([&completedTasks]() {
            ++completedTasks;
        }), "second destructor test task should enqueue");
    }

    expect(completedTasks == 2, "destructor should let queued tasks complete before joining");
}
}

int main() {
    try {
        testHttpParserParsesGetRootHttp11();
        testHttpParserHttp11ConnectionCloseDisablesKeepAlive();
        testHttpParserHttp10ClosesByDefault();
        testHttpParserHttp10ConnectionKeepAliveEnablesKeepAlive();
        testHttpParserParsesHeadRequest();
        testHttpParserParsesContentLength();
        testHttpParserRejectsInvalidContentLength();
        testHttpParserRejectsUnsupportedHttpVersion();
        testHttpParserRejectsPathWithoutLeadingSlash();
        testHttpParserRejectsInvalidHeader();
        testHttpParserKeepsRawUrlPath();
        testHttpParserLeavesInvalidUrlEncodingForHandler();
        testHttpParserStoresHeaderNamesCaseInsensitively();
        testHttpParserRejectsOverlongRequestLineAndHeaders();
        testHttpResponseSerializesStatusHeadersAndBody();
        testHttpResponseSerializesKeepAliveConnectionHeader();
        testHttpResponseCanSerializeHeadersWithoutBody();
        testHttpResponseSerializesCustomHeaders();
        testHttpResponseSupportsJsonBody();
        testRouterHandlesPingRoute();
        testRouterHandlesTimeRoute();
        testRouterEchoesRequestBody();
        testRouterReturnsNotFoundForUnknownApiRoute();
        testRouterIgnoresNonApiPaths();
        testRouterMatchesMethodAndPathForRegisteredRoutes();
        testWebSocketHandshakeComputesAcceptKey();
        testWebSocketHandshakeAcceptsValidRequest();
        testWebSocketHandshakeRejectsInvalidRequest();
        testWebSocketHandshakeSerializesSwitchingProtocolsResponse();
        testStaticFileHandlerServesIndexForRoot();
        testStaticFileHandlerServesIndexBeforeDirectoryListing();
        testStaticFileHandlerBuildsDirectoryListingWhenIndexMissing();
        testStaticFileHandlerCanDisableDirectoryListing();
        testStaticFileHandlerIgnoresRangeForDirectoryListing();
        testHeadDirectoryListingUsesHeadersWithoutBody();
        testStaticFileHandlerSkipsDirectoryEntriesOutsideRoot();
        testStaticFileHandlerReturnsNotFoundForMissingFile();
        testStaticFileHandlerServesPlainPath();
        testStaticFileHandlerGzipsSmallTextWhenAccepted();
        testStaticFileHandlerSkipsGzipWhenQualityIsZero();
        testStaticFileHandlerSkipsGzipForBinaryContent();
        testStaticFileHandlerSkipsGzipForRangeRequest();
        testStaticFileHandlerDecodesUrlEncodedSpace();
        testStaticFileHandlerRejectsInvalidUrlEncoding();
        testStaticFileHandlerIgnoresQueryString();
        testStaticFileHandlerRejectsTraversalPaths();
        testStaticFileHandlerMimeTypes();
        testStaticFileHandlerParsesExplicitByteRange();
        testStaticFileHandlerParsesOpenEndedByteRange();
        testStaticFileHandlerParsesSuffixByteRange();
        testStaticFileHandlerKeepsLargeFilesOnSendfilePath();
        testStaticFileHandlerKeepsLargeCompressibleFilesOnSendfilePath();
        testStaticFileHandlerKeepsLargeRangeOnSendfilePath();
        testStaticFileHandlerReloadsCachedFileWhenModified();
        testStaticFileHandlerAddsStableEtag();
        testStaticFileHandlerIfNoneMatchReturnsNotModified();
        testStaticFileHandlerIfNoneMatchSupportsListsAndWeakEtags();
        testStaticFileHandlerIfNoneMatchTakesPriorityOverRange();
        testStaticFileHandlerRangeRunsWhenIfNoneMatchMisses();
        testFileCacheEvictsLeastRecentlyUsedEntry();
        testStaticFileHandlerRejectsUnsatisfiableByteRange();
        testStaticFileHandlerRejectsInvalidByteRangeFormat();
        testHeadStaticFileRangeUsesPartialHeadersWithoutBody();
        testHeadStaticFileSuccessUsesGetHeadersWithoutBody();
        testHeadStaticFileNotFoundHasHeadersWithoutBody();
        testConfigParsesConnectionIdleTimeout();
        testConfigDefaultsRequestLimits();
        testConfigRejectsInvalidMaxRequestBodySize();
        testConfigRejectsInvalidDirectoryListingFlag();
        testConfigRejectsInvalidTlsFlag();
        testCommandLineUsesDefaultConfigPath();
        testCommandLineOverridesPort();
        testCommandLineOverridesMaxRequestBodySize();
        testCommandLineRejectsInvalidPort();
        testCommandLineRejectsInvalidMaxRequestBodySize();
        testCommandLineHelpOutput();
        testThreadPoolExecutesTask();
        testThreadPoolCreatesWorkerWhenThreadCountIsZero();
        testThreadPoolRejectsTaskWhenQueueIsFull();
        testThreadPoolDestructorJoinsWorkers();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
