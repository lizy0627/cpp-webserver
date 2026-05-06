#include "HttpParser.h"
#include "HttpResponse.h"
#include "StaticFileHandler.h"
#include "ThreadPool.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

void writeFile(const std::filesystem::path& path, const std::string& body) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to create test file");
    }
    file << body;
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

void testHttpParserDecodesUrlPath() {
    HttpRequest request;
    const bool parsed = HttpParser::parse(
        "GET /hello%20world%2Findex.html HTTP/1.1\r\n"
        "\r\n",
        request);

    expect(parsed, "URL encoded request should parse");
    expect(request.path == "/hello world/index.html", "path should be URL decoded");
}

void testHttpParserRejectsInvalidUrlEncoding() {
    expectParseFailure(
        "GET /hello%2 HTTP/1.1\r\n"
        "\r\n",
        "incomplete URL encoding should fail");

    expectParseFailure(
        "GET /hello%GG HTTP/1.1\r\n"
        "\r\n",
        "non-hex URL encoding should fail");
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
    expect(
        raw.rfind("\r\n\r\nhello") == raw.size() - std::string("\r\n\r\nhello").size(),
        "body should serialize after header separator");
}

void testStaticFileHandlerServesIndexForRoot() {
    StaticFileHandler handler(WEBSERVER_TEST_WWW_DIR);

    const StaticFileResult index = handler.handle("/");
    expect(index.status == StaticFileResult::Status::Ok, "index should be served");
    expect(index.contentType == "text/html; charset=utf-8", "index content type should be HTML");
    expect(!index.body.empty(), "index body should not be empty");
}

void testStaticFileHandlerReturnsNotFoundForMissingFile() {
    StaticFileHandler handler(WEBSERVER_TEST_WWW_DIR);

    const StaticFileResult missing = handler.handle("/missing-file.txt");
    expect(missing.status == StaticFileResult::Status::NotFound, "missing file should return 404");
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
        testHttpParserRejectsUnsupportedHttpVersion();
        testHttpParserRejectsPathWithoutLeadingSlash();
        testHttpParserRejectsInvalidHeader();
        testHttpParserDecodesUrlPath();
        testHttpParserRejectsInvalidUrlEncoding();
        testHttpParserStoresHeaderNamesCaseInsensitively();
        testHttpParserRejectsOverlongRequestLineAndHeaders();
        testHttpResponseSerializesStatusHeadersAndBody();
        testStaticFileHandlerServesIndexForRoot();
        testStaticFileHandlerReturnsNotFoundForMissingFile();
        testStaticFileHandlerRejectsTraversalPaths();
        testStaticFileHandlerMimeTypes();
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
