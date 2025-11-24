#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "LocationConfig.hpp"

class ConfigParser {
public:
    ConfigParser(const std::string& configFile);
    void parse();

    struct ServerConfig {
        std::vector<std::string> listenPorts;
        std::string serverName;
        std::string root;
        std::vector<std::string> indexFiles;
        std::map<int, std::string> errorPages;
        long clientMaxBodySize; // in bytes
        std::map<std::string, LocationConfig> locations;
        // Default LocationConfig for settings not overridden by a specific location block
        LocationConfig defaultLocationSettings;

        ServerConfig() : clientMaxBodySize(1024 * 1024) {} // Default 1MB
    };

    const std::vector<ServerConfig>& getServers() const;

private:
    std::string configFile;
    std::vector<ServerConfig> servers;

    // Helper methods for parsing
    void parseServerBlock(std::ifstream& file, std::string& line);
    void parseLocationBlock(std::ifstream& file, std::string& line, LocationConfig& location, bool isDefaultLocation);
    std::string trim(const std::string& str);
    std::vector<std::string> split(const std::string& s, char delimiter);

};

#endif // CONFIGPARSER_HPP