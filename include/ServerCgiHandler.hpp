#ifndef SERVER_CGI_HANDLER_HPP
#define SERVER_CGI_HANDLER_HPP

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "LocationConfig.hpp"

class ServerCgiHandler {
public:
    ServerCgiHandler(const LocationConfig& locConfig, HttpRequest& request);
    void executeCgi(HttpResponse& response);

private:
    const LocationConfig& _locConfig;
    HttpRequest& _request;
};

#endif // SERVER_CGI_HANDLER_HPP
