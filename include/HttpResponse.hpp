#ifndef HTTPRESPONSE_HPP
#define HTTPRESPONSE_HPP

#include <map>
#include <sstream>
#include <string>

class HttpResponse {
public:
    HttpResponse();
    void setStatus(int statusCode);
    void setHeader(const std::string& key, const std::string& value);
    void setBody(const std::string& body);
    const std::string& getBody() const;
    std::string generateResponse(bool isHead = false);
    int getStatus() const;
    bool hasHeader(const std::string& key) const;
    std::string getStatusMessage(int statusCode); // Moved from private to public

private:
    int statusCode;
    std::map<std::string, std::string> headers;
    std::string body;
};

#endif // HTTPRESPONSE_HPP