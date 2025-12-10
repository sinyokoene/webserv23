#include "HttpRequest.hpp"
#include "Utils.hpp"

HttpRequest::HttpRequest() {
    // Initialize request data
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
    std::string headersBlock;
    // We need to pass the rest of the stream to parseHeaders, or extract the string.
    // Since we already have the string in headSection, we can just find the first newline and substring.
    size_t firstNewline = headSection.find('\n');
    if (firstNewline != std::string::npos) {
         headersBlock = headSection.substr(firstNewline + 1);
         headers = parseHeaders(headersBlock);
    }

    // Exact body bytes after header separator
    if (headerEnd != std::string::npos) {
        body = request.substr(headerEnd + sepLen);
    } else {
        body.clear();
    }
}

std::map<std::string, std::string> HttpRequest::parseHeaders(const std::string& headerBlock) {
    std::map<std::string, std::string> headers;
    std::istringstream hs(headerBlock);
    std::string line;
    while (std::getline(hs, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = toLower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        size_t first = value.find_first_not_of(" \t");
        size_t last = value.find_last_not_of(" \t");
        if (first != std::string::npos) value = value.substr(first, last - first + 1); else value = "";
        headers[name] = value;
    }
    return headers;
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
    std::string lower = toLower(headerName);
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

bool HttpRequest::decodeChunkedBody(const std::string& data, size_t startPos, size_t& consumed, std::string& out) {
    size_t pos = startPos;
    out.clear();
    while (true) {
        size_t lineEnd = data.find("\r\n", pos);
        if (lineEnd == std::string::npos) return false;
        std::string sizeLine = data.substr(pos, lineEnd - pos);
        size_t sc = sizeLine.find(';');
        if (sc != std::string::npos) sizeLine = sizeLine.substr(0, sc);
        size_t f = sizeLine.find_first_not_of(" \t");
        size_t l = sizeLine.find_last_not_of(" \t");
        if (f == std::string::npos) return false;
        sizeLine = sizeLine.substr(f, l - f + 1);
        unsigned long chunkSize = 0;
        std::istringstream xs(sizeLine);
        xs >> std::hex >> chunkSize;
        if (xs.fail()) return false;
        pos = lineEnd + 2;
        if (chunkSize == 0) {
            size_t trailerEnd = data.find("\r\n", pos);
            if (trailerEnd == std::string::npos) return false;
            consumed = trailerEnd + 2;
            return true;
        }
        if (data.size() < pos + chunkSize + 2) return false;
        out.append(data, pos, chunkSize);
        pos += chunkSize;
        if (!(data[pos] == '\r' && data[pos + 1] == '\n')) return false;
        pos += 2;
    }
}

std::string HttpRequest::normalizeChunkedRequest(const std::string& buffer, size_t headerEnd, const std::string& decodedBody) {
    std::string reqLine = buffer.substr(0, buffer.find("\r\n"));
    std::string headersOnly = buffer.substr(buffer.find("\r\n") + 2, headerEnd - (buffer.find("\r\n") + 2));
    std::istringstream hsin(headersOnly);
    std::string line;
    std::string rebuiltHeaders;
    while (std::getline(hsin, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string lower = toLower(line.substr(0, colon));
        if (lower == "transfer-encoding" || lower == "content-length") continue;
        rebuiltHeaders += line + "\r\n";
    }
    std::ostringstream cl;
    cl << "Content-Length: " << decodedBody.size() << "\r\n";
    rebuiltHeaders += cl.str();
    return reqLine + "\r\n" + rebuiltHeaders + "\r\n" + decodedBody;
}

bool HttpRequest::wantsKeepAlive() const {
    std::string connHeader = getHeader("connection");
    std::string connLower = toLower(connHeader);
    if (version == "HTTP/1.1") {
        return (connLower != "close");
    }
    return (connLower == "keep-alive");
}
