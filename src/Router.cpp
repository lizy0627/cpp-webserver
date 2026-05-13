#include "Router.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {
std::tm localTime(std::time_t value) {
    std::tm result {};
#if defined(_WIN32)
    localtime_s(&result, &value);
#else
    localtime_r(&value, &result);
#endif
    return result;
}

std::string currentServerTime() {
    const std::time_t now = std::time(nullptr);
    const std::tm local = localTime(now);

    std::ostringstream stream;
    stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}
}

Router::Router() {
    addRoute("GET", "/api/ping", [](const HttpRequest&) {
        return Router::jsonResponse(200, "OK", "{\"message\":\"pong\"}");
    });

    addRoute("GET", "/api/time", [](const HttpRequest&) {
        return Router::jsonResponse(200, "OK", "{\"time\":\"" + currentServerTime() + "\"}");
    });

    addRoute("POST", "/api/echo", [](const HttpRequest& request) {
        HttpResponse response(200, "OK");
        const auto contentType = request.headers.find("content-type");
        if (contentType != request.headers.end()) {
            response.setContentType(contentType->second);
        }
        response.setBody(request.body);
        return response;
    });
}

void Router::addRoute(std::string method, std::string path, Handler handler) {
    routes_[{std::move(method), std::move(path)}] = std::move(handler);
}

bool Router::isApiPath(const std::string& requestPath) const {
    const std::string path = routePathFromRequestPath(requestPath);
    return path == "/api" || path.rfind("/api/", 0) == 0;
}

std::optional<HttpResponse> Router::handle(const HttpRequest& request) const {
    if (!isApiPath(request.path)) {
        return std::nullopt;
    }

    const auto route = routes_.find({request.method, routePathFromRequestPath(request.path)});
    HttpResponse response = route == routes_.end()
        ? jsonResponse(404, "Not Found", "{\"error\":\"not found\"}")
        : route->second(request);
    response.setKeepAlive(request.keepAlive);
    return response;
}

std::string Router::routePathFromRequestPath(const std::string& requestPath) {
    const std::size_t end = requestPath.find_first_of("?#");
    return end == std::string::npos ? requestPath : requestPath.substr(0, end);
}

HttpResponse Router::jsonResponse(int statusCode, const std::string& statusText, const std::string& body) {
    HttpResponse response(statusCode, statusText);
    response.setJsonBody(body);
    return response;
}
