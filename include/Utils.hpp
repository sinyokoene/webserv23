#ifndef UTILS_HPP
#define UTILS_HPP

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

// Function to split a string by a delimiter
std::vector<std::string> split(const std::string &s, char delimiter);

// Function to trim whitespace from both ends of a string
std::string trim(const std::string &s);

// Function to convert a string to lowercase
std::string toLower(const std::string &s);

// Function to check if a string starts with a given prefix
bool startsWith(const std::string &s, const std::string &prefix);

// Function to check if a string ends with a given suffix
bool endsWith(const std::string &s, const std::string &suffix);

#endif // UTILS_HPP