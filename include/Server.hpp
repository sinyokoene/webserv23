#ifndef SERVER_HPP
#define SERVER_HPP

#include <sys/select.h> // For fd_set
#include <ctime>        // For time_t
#include <map>
#include <set>
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
    void handleRequest(HttpRequest& request, HttpResponse& response);
    
private:
    void parseConfig(const std::string& configFile);
    const LocationConfig& findLocationConfig(const ConfigParser::ServerConfig& serverConfig, const std::string& path) const;
    std::string resolvePath(const std::string& basePath, const std::string& relativePath) const;
    void serveErrorPage(HttpResponse& response, int statusCode, const ConfigParser::ServerConfig& config);
    std::set<std::string> getAllowedMethodsForPath(const std::string& path, const ConfigParser::ServerConfig& config) const;

    // New helper methods for refactoring start()
    void processClientData(int clientSocket,
                           fd_set& master_set,
                           int& fdmax,
                           std::map<int, std::string>& clientBuffers,
                           std::map<int, std::string>& responseBuffers,
                           std::map<int, size_t>& sendOffsets,
                           std::map<int, bool>& keepAliveMap,
                           std::map<int, time_t>& lastActivity);
    void dispatchRequest(HttpRequest& request, HttpResponse& response, const ConfigParser::ServerConfig& config);
    
    // Specific HTTP method handlers
    void handleGetHeadRequest(HttpRequest& request, HttpResponse& response, const ConfigParser::ServerConfig& config, const LocationConfig& locConfig, const std::string& effectiveRoot, bool isHead);
    void handlePostRequest(HttpRequest& request, HttpResponse& response, const ConfigParser::ServerConfig& config, const LocationConfig& locConfig, const std::string& effectiveRoot);
    void handlePutRequest(HttpRequest& request, HttpResponse& response, const ConfigParser::ServerConfig& config, const LocationConfig& locConfig, const std::string& effectiveRoot);
    void handleDeleteRequest(HttpRequest& request, HttpResponse& response, const ConfigParser::ServerConfig& config, const LocationConfig& locConfig, const std::string& effectiveRoot);
    void handleOptionsRequest(HttpRequest& request, HttpResponse& response, const ConfigParser::ServerConfig& config);
    void handleCgiRequest(HttpRequest& request, HttpResponse& response,
                          const ConfigParser::ServerConfig& config,
                          const LocationConfig& locConfig,
                          const std::string& effectiveRoot);

    std::string getMimeType(const std::string& filePath) const;

    std::vector<ConfigParser::ServerConfig> serverConfigs; // Stores all parsed server blocks
    ConfigParser::ServerConfig currentConfig; // The active configuration for this server instance
    std::string configPath; // path used to start this server
    
    std::vector<int> serverSockets;
};

#endif // SERVER_HPP