#include "HttpResponse.hpp"

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
        case 100: return "Continue";
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

std::string HttpResponse::getMimeType(const std::string& path) {
    size_t dotPos = path.find_last_of(".");
    if (dotPos == std::string::npos) {
        return "application/octet-stream"; 
    }
    std::string ext = path.substr(dotPos + 1);
    
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "js") return "text/javascript";
    if (ext == "txt") return "text/plain";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "png") return "image/png";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    if (ext == "pdf") return "application/pdf";
    if (ext == "json") return "application/json";
    if (ext == "xml") return "application/xml";
    return "application/octet-stream";
}

void HttpResponse::setDefaultErrorBody() {
    body = "<html><body><h1>" + getStatusMessage(statusCode) + "</h1></body></html>";
    setHeader("Content-Type", "text/html");
}

void HttpResponse::setAllowHeader(const std::set<std::string>& methods) {
    std::string allowHeader;
    for (std::set<std::string>::const_iterator it = methods.begin(); it != methods.end(); ++it) {
        if (!allowHeader.empty()) allowHeader += ", ";
        allowHeader += *it;
    }
    setHeader("Allow", allowHeader);
}
