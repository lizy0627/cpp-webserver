#include "HttpResponse.h"

#include <utility>

HttpResponse::HttpResponse()
    : statusCode_(200),
      statusText_("OK"),
      contentType_("text/plain; charset=utf-8") {}

HttpResponse::HttpResponse(int statusCode, std::string statusText)
    : statusCode_(statusCode),
      statusText_(std::move(statusText)),
      contentType_("text/plain; charset=utf-8") {}

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

int HttpResponse::statusCode() const {
    return statusCode_;
}

std::string HttpResponse::toString() const {
    return "HTTP/1.1 " + std::to_string(statusCode_) + " " + statusText_ + "\r\n"
        "Content-Type: " + contentType_ + "\r\n"
        "Content-Length: " + std::to_string(body_.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body_;
}
