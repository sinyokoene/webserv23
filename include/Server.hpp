#ifndef SERVER_HPP
#define SERVER_HPP

#include <arpa/inet.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ConfigParser.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "LocationConfig.hpp"

// Structure to track CGI state for non-blocking handling
struct CgiState {
    pid_t pid;
    int pipe_in;  // Write to CGI stdin
    int pipe_out; // Read from CGI stdout
    std::string bodyToWrite;
    size_t bodyWritten;
    std::string cgiOutput;
    bool writeComplete;
    bool readComplete;
    time_t startTime;
    time_t lastIO;
    HttpRequest request;
    const ConfigParser::ServerConfig* config;
    LocationConfig locConfig;
    std::string effectiveRoot;
    bool isHead;
    
    CgiState() : pid(0), pipe_in(-1), pipe_out(-1), bodyWritten(0), 
                 writeComplete(false), readComplete(false), 
                 startTime(0), lastIO(0), config(NULL), isHead(false) {}
};

// Per-connection file streaming state
struct FileStreamState {
    int fd;
    off_t offset;
    off_t size;
    bool active;
    bool isHead;
    std::string pendingChunk;

    FileStreamState() : fd(-1), offset(0), size(0), active(false), isHead(false), pendingChunk() {}
};

// Per-connection state tracked by the event loop
struct ClientState {
    std::string inBuffer;
    std::string outBuffer;
    size_t outOffset;
    bool keepAlive;
    bool closing;
    bool expectContinue;
    bool sentContinue;
    time_t lastActivity;
    int port;
    bool chunkedMode;
    bool chunkComplete;
    std::string chunkDecoded;
    size_t contentLength;
    size_t bodyStart;
    FileStreamState fileStream;

    ClientState()
        : outOffset(0),
          keepAlive(false),
          closing(false),
          expectContinue(false),
          sentContinue(false),
          lastActivity(0),
          port(0),
          chunkedMode(false),
          chunkComplete(false),
          contentLength(0),
          bodyStart(0) {}
};

class Server {
public:
    Server(const std::string& configFile);
    void start();
    void stop();
    
private:
    void parseConfig(const std::string& configFile);
    std::pair<std::string, const LocationConfig*> matchLocation(const ConfigParser::ServerConfig& serverConfig, const std::string& path) const;
    const LocationConfig& findLocationConfig(const ConfigParser::ServerConfig& serverConfig, const std::string& path) const;
    std::string resolvePath(const ConfigParser::ServerConfig& config, const std::string& basePath, const std::string& relativePath) const;
    void serveErrorPage(HttpResponse& response, int statusCode, const ConfigParser::ServerConfig& config);
    std::set<std::string> getAllowedMethodsForPath(const std::string& path, const ConfigParser::ServerConfig& config) const;

    void dispatchRequest(int clientFd, HttpRequest& request, HttpResponse& response, 
                         const ConfigParser::ServerConfig& config, bool& responsReady, ClientState& state);
    
    // Specific HTTP method handlers
    void handleGetHeadRequest(HttpRequest& request, HttpResponse& response,
                              const ConfigParser::ServerConfig& config,
                              const LocationConfig& locConfig,
                              const std::string& effectiveRoot,
                              bool isHead,
                              FileStreamState& streamPlan);
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
    
    // CGI Handler (now non-blocking)
    bool startCgiRequest(int clientFd, HttpRequest& request,
                          const ConfigParser::ServerConfig& config,
                          const LocationConfig& locConfig,
                         const std::string& effectiveRoot,
                         bool isHead);
    
    // CGI helpers for main loop
    void handleCgiWrite(int clientFd, CgiState& cgi);
    void handleCgiRead(int clientFd, CgiState& cgi);
    void finalizeCgiRequest(int clientFd, CgiState& cgi, int status, std::string& responseBuffer);
                          
    // Utility
    std::string getMimeType(const std::string& path) const;
    
    // Config selection
    const ConfigParser::ServerConfig& selectConfig(int port, const std::string& hostHeader) const;

    // Event-loop helpers to keep start() readable
    void buildPortMapping(std::set<int>& portsToBind);
    bool bindListeningSockets(const std::set<int>& portsToBind);
    void initMasterFdSets(fd_set& master_read, fd_set& master_write, int& fdmax) const;
    void buildFdSets(const fd_set& master_read, const fd_set& master_write,
                     fd_set& read_fds, fd_set& write_fds, int fdmax, int& loopFdMax);
    void handleClientTimeouts(std::map<int, ClientState>& clients,
                              fd_set& master_read, fd_set& master_write, time_t now);
    void handleCgiTimeouts(std::map<int, ClientState>& clients,
                           fd_set& master_write, int& fdmax, time_t now);
    void acceptConnections(fd_set& master_read, int& fdmax,
                           std::map<int, ClientState>& clients, time_t now);
    void processCgiIo(fd_set& read_fds, fd_set& write_fds,
                      fd_set& master_write, int& fdmax,
                      std::map<int, ClientState>& clients);
    void processClientReads(fd_set& read_fds, fd_set& master_read, fd_set& master_write,
                            int& fdmax, std::map<int, ClientState>& clients,
                            time_t now);
    void processClientWrites(fd_set& write_fds, fd_set& master_read, fd_set& master_write,
                             std::map<int, ClientState>& clients, time_t now);

    std::string configPath;
    std::vector<ConfigParser::ServerConfig> serverConfigs;
    ConfigParser::ServerConfig currentConfig; // Fallback
    std::vector<int> serverSockets;
    
    // Mapping from port to list of configs (for multi-port/host support)
    std::map<int, std::vector<const ConfigParser::ServerConfig*> > portToConfigs;
    // Mapping from server socket fd to port
    std::map<int, int> socketPortMap;
    
    // CGI state tracking (client fd -> CGI state)
    std::map<int, CgiState> cgiStates;
};

#endif // SERVER_HPP
