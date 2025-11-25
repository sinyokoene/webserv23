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

private:
    std::string method;
    std::string path;
    std::string queryString; // Added
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;
};

#endif // HTTPREQUEST_HPP
