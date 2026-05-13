#include "WebSocketHandshake.h"

#include "HttpRequest.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {
constexpr char websocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string routePathFromRequestPath(const std::string& requestPath) {
    const std::size_t end = requestPath.find_first_of("?#");
    return end == std::string::npos ? requestPath : requestPath.substr(0, end);
}

std::string toLower(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    return value;
}

std::string trimLinearWhitespace(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }

    const std::size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

bool hasHeaderToken(const std::string& value, const std::string& token) {
    std::size_t tokenStart = 0;
    while (tokenStart <= value.size()) {
        const std::size_t tokenEnd = value.find(',', tokenStart);
        const std::string currentToken = trimLinearWhitespace(value.substr(
            tokenStart,
            tokenEnd == std::string::npos ? std::string::npos : tokenEnd - tokenStart));

        if (toLower(currentToken) == token) {
            return true;
        }

        if (tokenEnd == std::string::npos) {
            break;
        }

        tokenStart = tokenEnd + 1;
    }

    return false;
}

int base64Value(char character) {
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }

    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 26;
    }

    if (character >= '0' && character <= '9') {
        return character - '0' + 52;
    }

    if (character == '+') {
        return 62;
    }

    if (character == '/') {
        return 63;
    }

    return -1;
}

std::optional<std::string> base64Decode(const std::string& encoded) {
    if (encoded.empty() || encoded.size() % 4 != 0) {
        return std::nullopt;
    }

    std::string decoded;
    decoded.reserve((encoded.size() / 4) * 3);

    int bitBuffer = 0;
    int bitCount = 0;
    bool paddingStarted = false;
    std::size_t paddingCount = 0;

    for (char character : encoded) {
        if (character == '=') {
            paddingStarted = true;
            ++paddingCount;
            if (paddingCount > 2) {
                return std::nullopt;
            }
            continue;
        }

        if (paddingStarted) {
            return std::nullopt;
        }

        const int value = base64Value(character);
        if (value < 0) {
            return std::nullopt;
        }

        bitBuffer = (bitBuffer << 6) | value;
        bitCount += 6;
        while (bitCount >= 8) {
            bitCount -= 8;
            decoded.push_back(static_cast<char>((bitBuffer >> bitCount) & 0xff));
        }
    }

    return decoded;
}

std::string base64Encode(const std::array<std::uint8_t, 20>& bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);

    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t remaining = bytes.size() - i;
        const std::uint32_t octetA = bytes[i];
        const std::uint32_t octetB = remaining > 1 ? bytes[i + 1] : 0;
        const std::uint32_t octetC = remaining > 2 ? bytes[i + 2] : 0;
        const std::uint32_t triple = (octetA << 16) | (octetB << 8) | octetC;

        encoded.push_back(alphabet[(triple >> 18) & 0x3f]);
        encoded.push_back(alphabet[(triple >> 12) & 0x3f]);
        encoded.push_back(remaining > 1 ? alphabet[(triple >> 6) & 0x3f] : '=');
        encoded.push_back(remaining > 2 ? alphabet[triple & 0x3f] : '=');
    }

    return encoded;
}

std::uint32_t rotateLeft(std::uint32_t value, unsigned int bits) {
    return (value << bits) | (value >> (32 - bits));
}

std::array<std::uint8_t, 20> sha1(const std::string& input) {
    std::vector<std::uint8_t> message(input.begin(), input.end());
    const std::uint64_t bitLength = static_cast<std::uint64_t>(message.size()) * 8;

    message.push_back(0x80);
    while ((message.size() % 64) != 56) {
        message.push_back(0);
    }

    for (int shift = 56; shift >= 0; shift -= 8) {
        message.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xff));
    }

    std::uint32_t h0 = 0x67452301;
    std::uint32_t h1 = 0xefcdab89;
    std::uint32_t h2 = 0x98badcfe;
    std::uint32_t h3 = 0x10325476;
    std::uint32_t h4 = 0xc3d2e1f0;

    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64) {
        std::uint32_t words[80] {};
        for (std::size_t i = 0; i < 16; ++i) {
            const std::size_t offset = chunk + i * 4;
            words[i] =
                (static_cast<std::uint32_t>(message[offset]) << 24) |
                (static_cast<std::uint32_t>(message[offset + 1]) << 16) |
                (static_cast<std::uint32_t>(message[offset + 2]) << 8) |
                static_cast<std::uint32_t>(message[offset + 3]);
        }

        for (std::size_t i = 16; i < 80; ++i) {
            words[i] = rotateLeft(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
        }

        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;

        for (std::size_t i = 0; i < 80; ++i) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5a827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdc;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6;
            }

            const std::uint32_t temp = rotateLeft(a, 5) + f + e + k + words[i];
            e = d;
            d = c;
            c = rotateLeft(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    const std::uint32_t hashWords[] = {h0, h1, h2, h3, h4};
    std::array<std::uint8_t, 20> digest {};
    for (std::size_t i = 0; i < 5; ++i) {
        digest[i * 4] = static_cast<std::uint8_t>((hashWords[i] >> 24) & 0xff);
        digest[i * 4 + 1] = static_cast<std::uint8_t>((hashWords[i] >> 16) & 0xff);
        digest[i * 4 + 2] = static_cast<std::uint8_t>((hashWords[i] >> 8) & 0xff);
        digest[i * 4 + 3] = static_cast<std::uint8_t>(hashWords[i] & 0xff);
    }

    return digest;
}
}

bool WebSocketHandshake::isEndpoint(const HttpRequest& request) {
    return routePathFromRequestPath(request.path) == "/ws";
}

std::optional<std::string> WebSocketHandshake::acceptForRequest(const HttpRequest& request) {
    if (!isEndpoint(request) || request.method != "GET" || request.version != "HTTP/1.1") {
        return std::nullopt;
    }

    const auto upgrade = request.headers.find("upgrade");
    if (upgrade == request.headers.end() ||
        toLower(trimLinearWhitespace(upgrade->second)) != "websocket") {
        return std::nullopt;
    }

    const auto connection = request.headers.find("connection");
    if (connection == request.headers.end() || !hasHeaderToken(connection->second, "upgrade")) {
        return std::nullopt;
    }

    const auto version = request.headers.find("sec-websocket-version");
    if (version == request.headers.end() || trimLinearWhitespace(version->second) != "13") {
        return std::nullopt;
    }

    const auto key = request.headers.find("sec-websocket-key");
    if (key == request.headers.end()) {
        return std::nullopt;
    }

    const std::string trimmedKey = trimLinearWhitespace(key->second);
    const std::optional<std::string> decodedKey = base64Decode(trimmedKey);
    if (!decodedKey.has_value() || decodedKey->size() != 16) {
        return std::nullopt;
    }

    return acceptForKey(trimmedKey);
}

std::string WebSocketHandshake::acceptForKey(const std::string& key) {
    return base64Encode(sha1(key + websocketGuid));
}

std::string WebSocketHandshake::switchingProtocolsResponse(const std::string& accept) {
    return "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n"
        "\r\n";
}
