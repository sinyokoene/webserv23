#include "Utils.hpp"
#include <iostream>

// Function to trim whitespace from both ends of a string
std::string trim(const std::string &str) {
    if (str.empty()) {
        return "";
    }
    size_t first = 0;
    while (first < str.length() && std::isspace(static_cast<unsigned char>(str[first]))) {
        first++;
    }
    if (first == str.length()) { // All whitespace
        return "";
    }
    size_t last = str.length() - 1;
    while (last > first && std::isspace(static_cast<unsigned char>(str[last]))) {
        last--;
    }
    return str.substr(first, last - first + 1);
}

// Function to split a string by a delimiter
std::vector<std::string> split(const std::string &str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(trim(token));
    }
    return tokens;
}

// Function to convert a string to lowercase
std::string toLower(const std::string &str) {
    std::string lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::tolower);
    return lowerStr;
}

// Function to extract the basename from a path
std::string basenameLike(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

// Function to create directories recursively
bool createDirectoriesRecursively(const std::string& dirPath) {
    if (dirPath.empty()) return false;
    std::string path;
    size_t i = 0;
    if (dirPath[0] == '/') { path = "/"; i = 1; }
    for (; i < dirPath.size(); ++i) {
        char ch = dirPath[i];
        if (ch == '/') {
            if (!path.empty() && path[path.size()-1] != '/') {
                struct stat st;
                if (stat(path.c_str(), &st) != 0) {
                    if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) return false;
                } else if (!S_ISDIR(st.st_mode)) {
                    return false;
                }
            }
        }
        path += ch;
    }
    // final directory
    if (!path.empty()) {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) return false;
        } else if (!S_ISDIR(st.st_mode)) {
            return false;
        }
    }
    return true;
}
