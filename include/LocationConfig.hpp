#ifndef LOCATIONCONFIG_HPP
#define LOCATIONCONFIG_HPP

#include <map>
#include <string>
#include <vector>

class LocationConfig {
public:
    LocationConfig();
    ~LocationConfig();

    void setPath(const std::string& path);
    std::string getPath() const;

    void setRoot(const std::string& root);
    std::string getRoot() const;

    void setIndex(const std::string& index); // Should this be std::vector<std::string>? For now, keep as project has it.
    std::string getIndex() const; // Assuming single index for now, can be changed to vector later if needed;
    const std::vector<std::string>& getIndexFiles() const;
    void addIndexFile(const std::string& indexFile);

    void setAutoindex(bool autoindex);
    bool getAutoindex() const;

    void setMethods(const std::vector<std::string>& methods);
    const std::vector<std::string>& getMethods() const;

    void setRedirect(const std::string& redirect);
    std::string getRedirect() const;

    void setCgiPass(const std::string& cgiPass);
    std::string getCgiPass() const;

    void setUploadStore(const std::string& uploadStore);
    std::string getUploadStore() const;

private:
    std::string path;
    std::string root;
    std::string index;
    std::vector<std::string> indexFiles;
    bool autoindex;
    std::vector<std::string> methods;
    std::string redirect;
    std::string cgiPass;
    std::string uploadStore;
};

#endif // LOCATIONCONFIG_HPP