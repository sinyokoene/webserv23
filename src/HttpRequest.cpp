#include "HttpRequest.hpp"
#include <iostream>
#include <string>
#include <sstream>

HttpRequest::HttpRequest() {
    // Initialize request data
}

static inline std::string toLowerAscii(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i) {
        char c = out[i];
        if (c >= 'A' && c <= 'Z') out[i] = c + 32;
    }
    return out;
}

void HttpRequest::parseRequest(const std::string& request) {
    method.clear(); path.clear(); version.clear(); headers.clear(); body.clear(); queryString.clear();

    // Find header/body split
    size_t headerEnd = request.find("\r\n\r\n");
    size_t sepLen = 4;
    if (headerEnd == std::string::npos) {
        headerEnd = request.find("\n\n");
        sepLen = (headerEnd == std::string::npos) ? 0 : 2;
    }

    std::string headSection;
    if (headerEnd != std::string::npos) headSection = request.substr(0, headerEnd);
    else headSection = request; // no body separator yet; parse what we can

    // Parse request line
    std::istringstream hs(headSection);
    std::string requestLine;
    if (std::getline(hs, requestLine)) {
        if (!requestLine.empty() && requestLine[requestLine.size()-1] == '\r') requestLine.erase(requestLine.size()-1);
        std::istringstream lineStream(requestLine);
        std::string fullPath;
        lineStream >> method >> fullPath >> version;
        // Separate path and query string
        size_t queryPos = fullPath.find('?');
        if (queryPos != std::string::npos) {
            path = fullPath.substr(0, queryPos);
            queryString = fullPath.substr(queryPos + 1);
        } else {
            path = fullPath;
            queryString = "";
        }
    }

    // Remaining lines in headSection are headers
    std::string headerLine;
    while (std::getline(hs, headerLine)) {
        if (!headerLine.empty() && headerLine[headerLine.size()-1] == '\r') headerLine.erase(headerLine.size()-1);
        if (headerLine.empty()) break;
        size_t colonPos = headerLine.find(':');
        if (colonPos != std::string::npos) {
            std::string key = headerLine.substr(0, colonPos);
            std::string value = headerLine.substr(colonPos + 1);
            // trim leading space
            size_t first = value.find_first_not_of(" \t");
            if (first != std::string::npos) value = value.substr(first); else value.clear();
            // normalize header names to lowercase for case-insensitive access
            headers[toLowerAscii(key)] = value;
        }
    }

    // Exact body bytes after header separator
    if (headerEnd != std::string::npos) {
        body = request.substr(headerEnd + sepLen);
    } else {
        body.clear();
    }
}

std::string HttpRequest::getMethod() const {
    return method;
}

std::string HttpRequest::getPath() const {
    return path;
}

std::string HttpRequest::getHeader(const std::string& headerName) const {
    std::map<std::string, std::string>::const_iterator it = headers.find(headerName);
    if (it != headers.end()) {
        return it->second;
    }
    // case-insensitive lookup (headers are stored as lowercase)
    std::string lower = toLowerAscii(headerName);
    it = headers.find(lower);
    if (it != headers.end()) {
        return it->second;
    }
    return "";
}

std::string HttpRequest::getBody() const {
    return body;
}

std::string HttpRequest::getVersion() const {
    return version;
}

std::string HttpRequest::getQueryString() const {
    return queryString;
}

const std::map<std::string, std::string>& HttpRequest::getHeaders() const {
    return headers;
}

void HttpRequest::parseHeaders(const std::string& request) {
    // Deprecated: headers are parsed directly in parseRequest from the header section
    (void)request;
}