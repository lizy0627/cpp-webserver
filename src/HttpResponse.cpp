#include "HttpResponse.h"

#include <utility>

HttpResponse::HttpResponse()
    : statusCode_(200),
      statusText_("OK"),
      contentType_("text/plain; charset=utf-8"),
      keepAlive_(false) {}

HttpResponse::HttpResponse(int statusCode, std::string statusText)
    : statusCode_(statusCode),
      statusText_(std::move(statusText)),
      contentType_("text/plain; charset=utf-8"),
      keepAlive_(false) {}

void HttpResponse::setStatusCode(int statusCode) {
    statusCode_ = statusCode;
}

void HttpResponse::setStatusText(const std::string& statusText) {
    statusText_ = statusText;
}

void HttpResponse::setStatus(int statusCode, const std::string& statusText) {
    statusCode_ = statusCode;
    statusText_ = statusText;
}

void HttpResponse::setContentType(const std::string& contentType) {
    contentType_ = contentType;
}

void HttpResponse::setBody(const std::string& body) {
    body_ = body;
}

void HttpResponse::setKeepAlive(bool keepAlive) {
    keepAlive_ = keepAlive;
}

int HttpResponse::statusCode() const {
    return statusCode_;
}

std::string HttpResponse::toString(bool includeBody) const {
    std::string response = "HTTP/1.1 " + std::to_string(statusCode_) + " " + statusText_ + "\r\n"
        "Content-Type: " + contentType_ + "\r\n"
        "Content-Length: " + std::to_string(body_.size()) + "\r\n"
        "Connection: " + (keepAlive_ ? std::string("keep-alive") : std::string("close")) + "\r\n"
        "\r\n";

    if (includeBody) {
        response += body_;
    }

    return response;
}
