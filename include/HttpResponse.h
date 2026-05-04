#pragma once

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

    int statusCode() const;
    std::string toString() const;

private:
    int statusCode_;
    std::string statusText_;
    std::string contentType_;
    std::string body_;
};
