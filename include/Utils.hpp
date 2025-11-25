#ifndef UTILS_HPP
#define UTILS_HPP

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h> // for stat
#include <unistd.h>   // for access
#include <cerrno>     // for errno

// Function to split a string by a delimiter
std::vector<std::string> split(const std::string &s, char delimiter);

// Function to trim whitespace from both ends of a string
std::string trim(const std::string &s);

// Function to convert a string to lowercase
std::string toLower(const std::string &s);

// Function to extract the basename from a path
std::string basenameLike(const std::string& path);

// Function to create directories recursively
bool createDirectoriesRecursively(const std::string& dirPath);

// Function to delete directories recursively
bool deleteDirectoryRecursively(const std::string& path);

#endif // UTILS_HPP
