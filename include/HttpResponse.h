#pragma once

#include <cstddef>
#include <map>
#include <string>

class HttpResponse {
public:
    HttpResponse();
    HttpResponse(int statusCode, std::string statusText);

    void setStatusCode(int statusCode);
    void setStatusText(const std::string& statusText);
    void setStatus(int statusCode, const std::string& statusText);
    void setContentType(const std::string& contentType);
    void setBody(const std::string& body);
    void setJsonBody(const std::string& body);
    void setContentLength(std::size_t contentLength);
    void setHeader(const std::string& name, const std::string& value);
    void setKeepAlive(bool keepAlive);

    int statusCode() const;
    std::size_t bodySize() const;
    std::string toString(bool includeBody = true) const;

private:
    int statusCode_;
    std::string statusText_;
    std::string contentType_;
    std::string body_;
    std::size_t contentLength_;
    std::map<std::string, std::string> headers_;
    bool keepAlive_;
};
