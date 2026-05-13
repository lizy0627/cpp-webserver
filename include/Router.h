#pragma once

#include "HttpRequest.h"
#include "HttpResponse.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>

class Router {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    Router();

    void addRoute(std::string method, std::string path, Handler handler);
    bool isApiPath(const std::string& requestPath) const;
    std::optional<HttpResponse> handle(const HttpRequest& request) const;

private:
    using RouteKey = std::pair<std::string, std::string>;

    static std::string routePathFromRequestPath(const std::string& requestPath);
    static HttpResponse jsonResponse(int statusCode, const std::string& statusText, const std::string& body);

    std::map<RouteKey, Handler> routes_;
};
