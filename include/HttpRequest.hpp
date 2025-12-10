#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

#include <iostream>
#include <map>
#include <sstream>
#include <string>

class HttpRequest {
public:
    HttpRequest();
    void parseRequest(const std::string& request);
    std::string getMethod() const;
    std::string getPath() const;
    std::string getHeader(const std::string& header) const;
    std::string getBody() const;
    std::string getVersion() const;
    std::string getQueryString() const; // Added
    const std::map<std::string, std::string>& getHeaders() const; // Added
    static bool decodeChunkedBody(const std::string& data, size_t startPos, size_t& consumed, std::string& out);
    static std::map<std::string, std::string> parseHeaders(const std::string& headerBlock);
    static std::string normalizeChunkedRequest(const std::string& buffer, size_t headerEnd, const std::string& decodedBody);
    bool wantsKeepAlive() const;

private:
    std::string method;
    std::string path;
    std::string queryString; // Added
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;
};

#endif // HTTPREQUEST_HPP
