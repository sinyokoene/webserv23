#ifndef SERVER_HANDLERS_HPP
#define SERVER_HANDLERS_HPP

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "LocationConfig.hpp"

// Function declarations for handlers that are defined in ServerHandlers.cpp

void handleGetHead(HttpRequest& request, HttpResponse& response, const LocationConfig& locConfig);
void handlePost(HttpRequest& request, HttpResponse& response, const LocationConfig& locConfig);
void handleDelete(HttpRequest& request, HttpResponse& response, const LocationConfig& locConfig);
void handleOptions(HttpRequest& request, HttpResponse& response, const LocationConfig& locConfig);
void handleMethodNotAllowed(HttpResponse& response, const std::string& path);

#endif // SERVER_HANDLERS_HPP
