#include "HttpResponse.h"

#include <utility>

HttpResponse::HttpResponse()
    : statusCode_(200),
      statusText_("OK"),
      contentType_("text/plain; charset=utf-8"),
      contentLength_(0),
      keepAlive_(false) {}

HttpResponse::HttpResponse(int statusCode, std::string statusText)
    : statusCode_(statusCode),
      statusText_(std::move(statusText)),
      contentType_("text/plain; charset=utf-8"),
      contentLength_(0),
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
    contentLength_ = body_.size();
}

void HttpResponse::setJsonBody(const std::string& body) {
    setContentType("application/json");
    setBody(body);
}

void HttpResponse::setContentLength(std::size_t contentLength) {
    contentLength_ = contentLength;
}

void HttpResponse::setHeader(const std::string& name, const std::string& value) {
    headers_[name] = value;
}

void HttpResponse::setKeepAlive(bool keepAlive) {
    keepAlive_ = keepAlive;
}

int HttpResponse::statusCode() const {
    return statusCode_;
}

std::size_t HttpResponse::bodySize() const {
    return contentLength_;
}

std::string HttpResponse::toString(bool includeBody) const {
    std::string response = "HTTP/1.1 " + std::to_string(statusCode_) + " " + statusText_ + "\r\n"
        "Content-Type: " + contentType_ + "\r\n"
        "Content-Length: " + std::to_string(contentLength_) + "\r\n"
        "Connection: " + (keepAlive_ ? std::string("keep-alive") : std::string("close")) + "\r\n";

    for (const auto& header : headers_) {
        response += header.first + ": " + header.second + "\r\n";
    }

    response += "\r\n";

    if (includeBody) {
        response += body_;
    }

    return response;
}
