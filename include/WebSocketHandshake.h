#pragma once

#include <optional>
#include <string>

class HttpRequest;

namespace WebSocketHandshake {
bool isEndpoint(const HttpRequest& request);
std::optional<std::string> acceptForRequest(const HttpRequest& request);
std::string acceptForKey(const std::string& key);
std::string switchingProtocolsResponse(const std::string& accept);
}
