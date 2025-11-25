#ifndef SERVER_HPP
#define SERVER_HPP

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <sys/select.h> // For fd_set
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime> // For time_t
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "ConfigParser.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "LocationConfig.hpp"

class Server {
public:
    Server(const std::string& configFile);
    void start();
    void stop();
    
private:
    void parseConfig(const std::string& configFile);
    const LocationConfig& findLocationConfig(const ConfigParser::ServerConfig& serverConfig, const std::string& path) const;
    std::string resolvePath(const std::string& basePath, const std::string& relativePath) const;
    void serveErrorPage(HttpResponse& response, int statusCode, const ConfigParser::ServerConfig& config);
    std::set<std::string> getAllowedMethodsForPath(const std::string& path, const ConfigParser::ServerConfig& config) const;

    void dispatchRequest(HttpRequest& request, HttpResponse& response, const ConfigParser::ServerConfig& config);
    
    // Specific HTTP method handlers
    void handleGetHeadRequest(HttpRequest& request, HttpResponse& response,
                              const ConfigParser::ServerConfig& config,
                              const LocationConfig& locConfig,
                              const std::string& effectiveRoot,
                              bool isHead);
    void handlePostRequest(HttpRequest& request, HttpResponse& response,
                           const ConfigParser::ServerConfig& config,
                           const LocationConfig& locConfig,
                           const std::string& effectiveRoot);
    void handlePutRequest(HttpRequest& request, HttpResponse& response,
                          const ConfigParser::ServerConfig& config,
                          const LocationConfig& locConfig,
                          const std::string& effectiveRoot);
    void handleDeleteRequest(HttpRequest& request, HttpResponse& response,
                             const ConfigParser::ServerConfig& config,
                             const LocationConfig& locConfig,
                             const std::string& effectiveRoot);
    void handleOptionsRequest(HttpRequest& request, HttpResponse& response,
                              const ConfigParser::ServerConfig& config);
    
    // CGI Handler
    void handleCgiRequest(HttpRequest& request, HttpResponse& response,
                          const ConfigParser::ServerConfig& config,
                          const LocationConfig& locConfig,
                          const std::string& effectiveRoot);
                          
    // Utility
    std::string getMimeType(const std::string& path) const;

    std::string configPath;
    std::vector<ConfigParser::ServerConfig> serverConfigs;
    ConfigParser::ServerConfig currentConfig; // For simplicity, using first config or currently matched one
    std::vector<int> serverSockets;
};

#endif // SERVER_HPP
