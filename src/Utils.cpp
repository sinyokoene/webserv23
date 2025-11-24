#include "Utils.hpp"

// Function to trim whitespace from both ends of a string
std::string trim(const std::string &str) {
    size_t first = str.find_first_not_of(' ');
    if (first == std::string::npos) return ""; // No content
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

// Function to split a string by a delimiter
std::vector<std::string> split(const std::string &str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

// Function to check if a string is a valid IP address
bool isValidIP(const std::string& ip) {
    int dots = 0;
    int num = 0;
    int len = ip.length();
    int partLen = 0;
    for (int i = 0; i < len; ++i) {
        if (ip[i] == '.') {
            if (partLen == 0) return false;
            if (num < 0 || num > 255) return false;
            ++dots;
            num = 0;
            partLen = 0;
        } else if (ip[i] >= '0' && ip[i] <= '9') {
            num = num * 10 + (ip[i] - '0');
            ++partLen;
            if (partLen > 3) return false;
        } else {
            return false;
        }
    }
    if (dots != 3) return false;
    if (num < 0 || num > 255) return false;
    if (partLen == 0) return false;
    return true;
}

// Function to convert a string to lowercase
std::string toLower(const std::string &str) {
    std::string lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::tolower);
    return lowerStr;
}