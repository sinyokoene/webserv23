#include "HttpResponse.hpp"
#include <sstream>

HttpResponse::HttpResponse() : statusCode(200), body("") {}

void HttpResponse::setStatus(int code) {
    statusCode = code;
}

void HttpResponse::setHeader(const std::string& key, const std::string& value) {
    headers[key] = value;
}

void HttpResponse::setBody(const std::string& responseBody) {
    body = responseBody;
}

std::string HttpResponse::generateResponse(bool isHead) {
    std::ostringstream responseStream;
    responseStream << "HTTP/1.1 " << statusCode << " " << getStatusMessage(statusCode) << "\r\n";

    // Set Content-Length based on body size, unless it's already set (e.g. for CGI)
    if (headers.find("Content-Length") == headers.end()) {
        std::ostringstream oss;
        oss << body.size();
        setHeader("Content-Length", oss.str());
    }

    std::map<std::string, std::string>::const_iterator it;
    for (it = headers.begin(); it != headers.end(); ++it) {
        responseStream << it->first << ": " << it->second << "\r\n";
    }
    responseStream << "\r\n";

    if (!isHead) {
        responseStream << body;
    }
    
    return responseStream.str();
}

std::string HttpResponse::getStatusMessage(int statusCode) {
    switch (statusCode) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 413: return "Payload Too Large";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 405: return "Method Not Allowed";
        case 431: return "Request Header Fields Too Large";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        default: return "Unknown";
    }
}

int HttpResponse::getStatus() const {
    return statusCode;
}

bool HttpResponse::hasHeader(const std::string& key) const {
    return headers.find(key) != headers.end();
}

const std::string& HttpResponse::getBody() const {
    return body;
}