#include "ConfigParser.hpp"

// Helper function to trim whitespace from both ends of a string
std::string ConfigParser::trim(const std::string& str) {
    if (str.empty()) {
        return "";
    }
    size_t first = 0;
    while (first < str.length() && isspace(static_cast<unsigned char>(str[first]))) {
        first++;
    }
    if (first == str.length()) { // All whitespace
        return "";
    }
    size_t last = str.length() - 1;
    while (last > first && isspace(static_cast<unsigned char>(str[last]))) {
        last--;
    }
    return str.substr(first, last - first + 1);
}

// Helper function to split a string by a delimiter
std::vector<std::string> ConfigParser::split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(trim(token));
    }
    return tokens;
}

ConfigParser::ConfigParser(const std::string& configFile) : configFile(configFile) {
    // Constructor - configuration will be parsed when parse() is called
}

void ConfigParser::parse() {
    std::ifstream file(this->configFile.c_str());
    if (!file.is_open()) {
        throw std::runtime_error("Could not open the file: " + this->configFile);
    }
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line == "server {") {
            servers.push_back(ServerConfig());
            parseServerBlock(file, line);
        } else if (!line.empty()) {
            // Global directives can be parsed here if any, or throw error for unexpected token
             std::cerr << "Warning: Ignoring unexpected line outside of server block: " << line << std::endl;
        }
    }
    file.close();

    if (servers.empty()) {
        std::cerr << "Warning: No server blocks found in configuration. Using default server settings." << std::endl;
        // Optionally, create a default server configuration if none are parsed
        // servers.push_back(ServerConfig()); // Example: Add a default server
        // servers.back().listenPorts.push_back("8080"); // Default port
        // servers.back().root = "./www"; // Default root
        // servers.back().defaultLocationSettings.setRoot(servers.back().root);
    }
}

void ConfigParser::parseServerBlock(std::ifstream& file, std::string& line) {
    ServerConfig& currentServer = servers.back();
    // Initialize default location settings from server's root if not already set
    if (currentServer.defaultLocationSettings.getRoot().empty() && !currentServer.root.empty()) {
        currentServer.defaultLocationSettings.setRoot(currentServer.root);
    }


    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line == "}") { // End of server block
            // If server root is set, ensure default location inherits it if not explicitly set
            if (!currentServer.root.empty() && currentServer.defaultLocationSettings.getRoot().empty()) {
                 currentServer.defaultLocationSettings.setRoot(currentServer.root);
            }
            // Ensure there's at least one port, or add a default
            if (currentServer.listenPorts.empty()) {
                std::cerr << "Warning: Server block without listen directive. Defaulting to port 8080." << std::endl;
                currentServer.listenPorts.push_back("8080");
            }
            return;
        }

        std::string directive;
        std::string value;
        
        // Manual directive and value extraction
        size_t first_space = line.find_first_of(" \t");
        if (first_space != std::string::npos) {
            directive = line.substr(0, first_space);
            value = line.substr(first_space + 1);
        } else {
            directive = line; // Line might be just the directive (e.g. "}") or a directive without value
        }
        directive = trim(directive); // Trim the extracted directive
        value = trim(value);     // Trim the extracted value

        // Remove trailing semicolon if present from value
        if (!value.empty() && value[value.length() - 1] == ';') {
            value.erase(value.length() - 1);
            value = trim(value);
        }


        if (directive == "listen") {
            std::vector<std::string> ports = split(value, ' ');
            for (size_t i = 0; i < ports.size(); ++i) {
                if (!ports[i].empty()) currentServer.listenPorts.push_back(ports[i]);
            }
        } else if (directive == "server_name") {
            currentServer.serverName = value;
        } else if (directive == "root") {
            currentServer.root = value;
            // Update default location's root if it hasn't been set by a specific location block yet
            // or if this server root is more specific.
            if (currentServer.defaultLocationSettings.getRoot().empty() || currentServer.defaultLocationSettings.getRoot() == "./www") // A common default
                 currentServer.defaultLocationSettings.setRoot(value);
        } else if (directive == "index") {
            currentServer.indexFiles = split(value, ' ');
             currentServer.defaultLocationSettings.setIndex(value); // Assuming first index is default for locations
        } else if (directive == "error_page") {
            std::vector<std::string> parts = split(value, ' ');
            if (parts.size() >= 2) {
                std::string pagePath = parts[parts.size() - 1];
                for (size_t i = 0; i < parts.size() - 1; ++i) {
                    try {
                        int code;
                        std::istringstream converter(parts[i]);
                        if (!(converter >> code)) {
                             throw std::invalid_argument("Invalid number format for error code");
                        }
                        currentServer.errorPages[code] = pagePath;
                    } catch (const std::exception& e) {
                        std::cerr << "Warning: Invalid error code '" << parts[i] << "' in error_page directive: " << e.what() << std::endl;
                    }
                }
            }
        } else if (directive == "client_max_body_size") {
            try {
                char suffix = ' ';
                long size_val = 0;
                std::string sizeStr = value;
                if (!value.empty()) {
                    suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(value[value.length() - 1])));
                    if (suffix == 'k' || suffix == 'm' || suffix == 'g') {
                        sizeStr.erase(sizeStr.length() - 1);
                    } else {
                        suffix = ' ';
                    }
                }
                std::istringstream converter(sizeStr);
                if (!(converter >> size_val)) {
                    throw std::invalid_argument("Invalid number format for size");
                }

                if (suffix == 'k') size_val *= 1024;
                else if (suffix == 'm') size_val *= 1024 * 1024;
                else if (suffix == 'g') size_val *= 1024 * 1024 * 1024;
                currentServer.clientMaxBodySize = size_val;
            } catch (const std::exception& e) {
                std::cerr << "Warning: Invalid client_max_body_size '" << value << "'." << std::endl;
            }
        } else if (directive == "location") {
            std::string locationPath;
            std::string modifier;
            
            // The 'value' here is "path {" or "= path {" or "~ path {"
            // The actual path and modifier are part of the 'value' from the line 'location <modifier> <path> {'
            std::string path_and_brace = value; // value is already trimmed and semicolon removed

            size_t brace_pos = path_and_brace.rfind('{');
            std::string path_part;
            if (brace_pos != std::string::npos) {
                path_part = trim(path_and_brace.substr(0, brace_pos));
            } else {
                // This should not happen if the line is a valid location block start
                std::cerr << "Warning: Location directive for '" << value << "' does not end with '{'. Line: " << line << std::endl;
                continue;
            }

            std::istringstream pathIss(path_part);
            std::string firstToken, secondToken;
            pathIss >> firstToken;
            if (pathIss >> secondToken && !secondToken.empty()) { 
                if (firstToken == "=" || firstToken == "~" || firstToken == "~*") { 
                    modifier = firstToken;
                    locationPath = secondToken;
                } else {
                    // Path with space, not supported without quotes by this simple parser
                    // For now, take the first part as path
                    locationPath = firstToken; 
                    std::cerr << "Warning: Location path '/" << path_part << "/' might be complex or contain spaces. Using '" << locationPath << "'." << std::endl;
                }
            } else { 
                 locationPath = firstToken;
            }
            
            locationPath = trim(locationPath);

            // Check if the original line (not just value) actually ends with "{" for block start
            // This check is somewhat redundant now given brace_pos check above but kept for safety.
            std::string trimmed_line_for_brace_check = trim(line);
            if (trimmed_line_for_brace_check.empty() || trimmed_line_for_brace_check[trimmed_line_for_brace_check.length() - 1] != '{') {
                 std::cerr << "Warning: Location block for '" << locationPath << "' does not start with '{'. Line: " << line << std::endl;
                 continue;
            }


            LocationConfig newLocation;
            newLocation.setPath(locationPath);
            // Inherit server's root by default for the new location
            if (!currentServer.root.empty()) {
                newLocation.setRoot(currentServer.root);
            }
            // Inherit server's index files by default
            if (!currentServer.indexFiles.empty()) {
                 newLocation.setIndex(currentServer.indexFiles[0]); // Assuming single index for now
            }

            // std::cerr << "DEBUG: Parsing location block for path: '" << locationPath << "'" << std::endl;
            parseLocationBlock(file, line, newLocation, false);
            currentServer.locations[locationPath] = newLocation;
            // std::cerr << "DEBUG: Added location '" << locationPath << "' with root '" << newLocation.getRoot() << "'" << std::endl;
        }
        // Default settings for the server (applied if no specific location matches)
        // These are parsed like location block directives but applied to currentServer.defaultLocationSettings
        else if (directive == "autoindex" || directive == "allow_methods" || directive == "return" || directive == "cgi_pass" || directive == "upload_store" || directive == "index") {
             // Re-process this line as if it's inside a "default" location block
             // This is a bit of a hack; ideally, parseLocationBlock would be more generic
             // or we'd have a separate function for server-level location-like directives.
            std::string tempLineForDefaultLoc = directive + " " + value + ";"; // Reconstruct for parseLocationBlock
            parseLocationBlock(file, tempLineForDefaultLoc, currentServer.defaultLocationSettings, true);
        }
         else {
            std::cerr << "Warning: Unknown directive '" << directive << "' in server block." << std::endl;
        }
    }
}

void ConfigParser::parseLocationBlock(std::ifstream& file, std::string& line, LocationConfig& location, bool isDefaultSettingsParse) {
    // If isDefaultSettingsParse is true, 'line' contains the directive to parse directly.
    // Otherwise, we are inside a 'location {}' block and need to read lines until the matching '}'.

    std::cerr << "DEBUG: parseLocationBlock called for location '" << location.getPath() << "'" << std::endl;
    
    while (true) {
        if (!isDefaultSettingsParse) {
            if (!std::getline(file, line)) break; // EOF
            line = trim(line);
            if (line.empty() || line[0] == '#') {
                continue;
            }
            // Strip inline comments starting with '#'
            size_t hashPosIn = line.find('#');
            if (hashPosIn != std::string::npos) {
                line = trim(line.substr(0, hashPosIn));
                if (line.empty()) continue;
            }
            if (line == "}") { // End of this location block
                return;
            }
        }

        std::string directive;
        std::string loc_value;

        // Manual directive and value extraction
        size_t first_space = line.find_first_of(" \t");
        if (first_space != std::string::npos) {
            directive = line.substr(0, first_space);
            loc_value = line.substr(first_space + 1);
        } else {
            directive = line;
        }
        directive = trim(directive);
        loc_value = trim(loc_value);

        if (!loc_value.empty() && loc_value[loc_value.length() - 1] == ';') {
            loc_value.erase(loc_value.length() - 1);
            loc_value = trim(loc_value);
        }

        // Also strip inline comments from the value side, e.g. "root .; # comment"
        size_t hashPosVal = loc_value.find('#');
        if (hashPosVal != std::string::npos) {
            loc_value = trim(loc_value.substr(0, hashPosVal));
        }

        if (directive == "root") {
            location.setRoot(loc_value);
        } else if (directive == "index") {
            std::vector<std::string> indices = split(loc_value, ' ');
            if (!indices.empty()) {
                location.setIndex(indices[0]);
            }
        } else if (directive == "allow_methods" || directive == "methods") {
            std::vector<std::string> methods = split(loc_value, ' ');
            location.setMethods(methods);
        } else if (directive == "return") { 
            location.setRedirect(loc_value);
        } else if (directive == "autoindex") {
            location.setAutoindex(loc_value == "on");
        } else if (directive == "cgi_pass") {
            location.setCgiPass(loc_value);
        } else if (directive == "upload_store") {
            location.setUploadStore(loc_value);
        } else if (!isDefaultSettingsParse) {
            // Unknown directive inside a location block
            std::cerr << "Warning: Unknown directive '" << directive << "' in location block for path '" << location.getPath() << "'." << std::endl;
        } else if (isDefaultSettingsParse && directive != "autoindex" && directive != "allow_methods" && directive != "return" && directive != "cgi_pass" && directive != "upload_store" && directive != "index") {
            std::cerr << "Warning: Unexpected directive '" << directive << "' while parsing default server settings." << std::endl;
        }

        if (isDefaultSettingsParse) break; // Only one line to process for default settings
    }
}


const std::vector<ConfigParser::ServerConfig>& ConfigParser::getServers() const {
    return servers;
}

// Ensure LocationConfig.cpp has implementations for new methods
// Example for LocationConfig.cpp:
/*
#include "LocationConfig.hpp"

LocationConfig::LocationConfig() : autoindex(false) {
    // Default constructor
}

LocationConfig::~LocationConfig() {
    // Destructor
}

void LocationConfig::setPath(const std::string& path) { this->path = path; }
std::string LocationConfig::getPath() const { return this->path; }

void LocationConfig::setRoot(const std::string& root) { this->root = root; }
std::string LocationConfig::getRoot() const { return this->root; }

void LocationConfig::setIndex(const std::string& index) { this->index = index; }
std::string LocationConfig::getIndex() const { return this->index; }

void LocationConfig::setMethods(const std::vector<std::string>& methods) { this->methods = methods; }
std::vector<std::string> LocationConfig::getMethods() const { return this->methods; }

void LocationConfig::setRedirect(const std::string& redirect) { this->redirect = redirect; }
std::string LocationConfig::getRedirect() const { return this->redirect; }

void LocationConfig::setAutoindex(bool autoindex) { this->autoindex = autoindex; }
bool LocationConfig::getAutoindex() const { return this->autoindex; }

void LocationConfig::setCgiPass(const std::string& cgiPass) { this->cgiPass = cgiPass; }
std::string LocationConfig::getCgiPass() const { return this->cgiPass; }

void LocationConfig::setUploadStore(const std::string& uploadStore) { this->uploadStore = uploadStore; }
std::string LocationConfig::getUploadStore() const { return this->uploadStore; }
*/