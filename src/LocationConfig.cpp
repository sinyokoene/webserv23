#include "LocationConfig.hpp"
#include <vector>

LocationConfig::LocationConfig() : autoindex(false) {
    // Default constructor implementation
    // Initialize methods to common defaults if desired, e.g., GET, HEAD
    // methods.push_back("GET");
    // methods.push_back("HEAD");
}

LocationConfig::~LocationConfig() {
    // Destructor implementation
}

void LocationConfig::setPath(const std::string& path) {
    this->path = path;
}

std::string LocationConfig::getPath() const {
    return this->path;
}

void LocationConfig::setMethods(const std::vector<std::string>& methods) {
    this->methods = methods;
}

const std::vector<std::string>& LocationConfig::getMethods() const {
    return this->methods;
}

void LocationConfig::setRoot(const std::string& root) {
    this->root = root;
}

std::string LocationConfig::getRoot() const {
    return this->root;
}

void LocationConfig::setIndex(const std::string& index) {
    this->index = index;
}

std::string LocationConfig::getIndex() const {
    return this->index;
}

const std::vector<std::string>& LocationConfig::getIndexFiles() const {
    return this->indexFiles;
}

void LocationConfig::addIndexFile(const std::string& indexFile) {
    this->indexFiles.push_back(indexFile);
}

void LocationConfig::setAutoindex(bool autoindex) {
    this->autoindex = autoindex;
}

bool LocationConfig::getAutoindex() const {
    return this->autoindex;
}

void LocationConfig::setCgiPass(const std::string& cgiPass) {
    this->cgiPass = cgiPass;
}

std::string LocationConfig::getCgiPass() const {
    return this->cgiPass;
}

void LocationConfig::setUploadStore(const std::string& uploadStore) {
    this->uploadStore = uploadStore;
}

std::string LocationConfig::getUploadStore() const {
    return this->uploadStore;
}

void LocationConfig::setRedirect(const std::string& redirect) {
    this->redirect = redirect;
}

std::string LocationConfig::getRedirect() const {
    return this->redirect;
}

bool LocationConfig::isCgiPath(const std::string& requestPath) const {
    if (!cgiPass.empty()) return true;
    if (requestPath.find("/cgi-bin/") != std::string::npos) return true;
    if (requestPath.find(".php") != std::string::npos) return true;
    if (requestPath.find(".py") != std::string::npos) return true;
    if (requestPath.find(".cgi") != std::string::npos) return true;
    return false;
}