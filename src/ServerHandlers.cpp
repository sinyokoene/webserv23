#include "Server.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <cctype>
#include <ctime>
#include <iostream> // Added for std::cerr
#include <cstring>  // Added for strerror
#include <cstdio>   // Added for remove (though unistd.h might also provide it)

// Helpers
static inline std::string toLowerAscii(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i) { char c = out[i]; if (c >= 'A' && c <= 'Z') out[i] = c + 32; }
    return out;
}

static inline std::string basenameLike(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

static std::string extractFilenameFromContentDisposition(const std::string& headerValue) {
    // Try RFC5987 filename* first
    std::string low = toLowerAscii(headerValue);
    size_t fnstar = low.find("filename*=");
    if (fnstar != std::string::npos) {
        std::string rest = headerValue.substr(fnstar + 10);
        size_t sc = rest.find(';'); if (sc != std::string::npos) rest = rest.substr(0, sc);
        size_t apos = rest.find("''"); if (apos != std::string::npos) rest = rest.substr(apos + 2);
        // trim and unquote
        size_t f2 = rest.find_first_not_of(" \t"); if (f2 != std::string::npos) rest = rest.substr(f2); else rest.clear();
        if (!rest.empty() && (rest[0] == '"' || rest[0] == '\'')) { char q = rest[0]; size_t q2 = rest.find(q, 1); rest = (q2 != std::string::npos) ? rest.substr(1, q2 - 1) : rest.substr(1); }
        return basenameLike(rest);
    }
    // Fallback: filename=
    size_t fn = low.find("filename=");
    if (fn != std::string::npos) {
        std::string rest = headerValue.substr(fn + 9);
        size_t f2 = rest.find_first_not_of(" \t"); if (f2 != std::string::npos) rest = rest.substr(f2); else rest.clear();
        if (!rest.empty() && (rest[0] == '"' || rest[0] == '\'')) { char q = rest[0]; size_t q2 = rest.find(q, 1); rest = (q2 != std::string::npos) ? rest.substr(1, q2 - 1) : rest.substr(1); }
        else { size_t sc = rest.find(';'); if (sc != std::string::npos) rest = rest.substr(0, sc); }
        return basenameLike(rest);
    }
    return "";
}

static std::string suggestFilenameFromHeaders(const HttpRequest& request) {
    // Prefer X-Filename
    std::string suggested = request.getHeader("x-filename");
    if (!suggested.empty()) return basenameLike(suggested);
    // Try Content-Disposition
    std::string cd = request.getHeader("content-disposition");
    if (!cd.empty()) {
        std::string name = extractFilenameFromContentDisposition(cd);
        if (!name.empty()) return name;
    }
    return "";
}

static bool setContentLengthFromFile(HttpResponse& response, const std::string& filePath) {
    std::ifstream file(filePath.c_str(), std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    std::ostringstream sizeStr; sizeStr << size;
    response.setHeader("Content-Length", sizeStr.str());
    return true;
}

// Create directories recursively (like mkdir -p)
static bool createDirectoriesRecursively(const std::string& dirPath) {
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

// Handler for GET and HEAD requests
void Server::handleGetHeadRequest(HttpRequest& request, HttpResponse& response, 
                                 const ConfigParser::ServerConfig& config, 
                                 const LocationConfig& locConfig, 
                                 const std::string& effectiveRoot,
                                 bool isHead) {
    // Check if there's a redirect defined for this location
    if (!locConfig.getRedirect().empty()) {
        response.setStatus(301); // Moved Permanently
        response.setHeader("Location", locConfig.getRedirect());
        response.setBody("<html><body><h1>301 Permanent Doorgestuurd</h1><p>Het document is verplaatst naar <a href=\"" + 
                        locConfig.getRedirect() + "\">" + locConfig.getRedirect() + "</a></p></body></html>");
        return;
    }

    // Los het bestandspad op
    std::string resolvedPath = resolvePath(effectiveRoot, request.getPath());
    if (resolvedPath.empty()) {
        response.setStatus(403); // Verboden
        serveErrorPage(response, 403, config);
        return;
    }

    struct stat st;
    if (stat(resolvedPath.c_str(), &st) != 0) {
        response.setStatus(404); // Niet Gevonden
        serveErrorPage(response, 404, config);
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        // Directory handling without forcing a trailing-slash redirect
        // Try to find an index file first
        std::vector<std::string> indexFiles = config.indexFiles;
        std::string index = locConfig.getIndex();
        if (!index.empty() && std::find(indexFiles.begin(), indexFiles.end(), index) == indexFiles.end()) {
            indexFiles.insert(indexFiles.begin(), index);  // Prioriteit voor locatie-specifieke index
        }
        if (indexFiles.empty()) {
            indexFiles.push_back("index.html");  // Standaard fallback
        }

        std::string indexPath;
        for (size_t i = 0; i < indexFiles.size(); ++i) {
            std::string testPath = resolvePath(resolvedPath, indexFiles[i]);
            if (!testPath.empty() && stat(testPath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                indexPath = testPath;
                break;
            }
        }

        if (!indexPath.empty()) {
            // Serve the index file
            std::ifstream file(indexPath.c_str(), std::ios::binary);
            if (file) {
                std::ostringstream ss;
                ss << file.rdbuf();
                std::string fileContent = ss.str();
                if (!isHead) {
                    response.setBody(fileContent);
                }
            }
            response.setStatus(200);
            response.setHeader("Content-Type", getMimeType(indexPath));
            if (isHead) { setContentLengthFromFile(response, indexPath); }
        } else if (locConfig.getAutoindex()) {
            // Generate directory listing
            DIR *dir = opendir(resolvedPath.c_str());
            std::string html = "<!DOCTYPE html><html><head><title>Index van " + 
                             request.getPath() + "</title></head><body><h1>Index van " + 
                             request.getPath() + "</h1><ul>";
            
            if (dir) {
                struct dirent *entry;
                while ((entry = readdir(dir)) != NULL) {
                    std::string name = entry->d_name;
                    if (name != "." && name != "..") {
                        std::string href = request.getPath();
                        if (!href.empty() && href[href.length() - 1] != '/') href += "/";
                        href += name;
                        html += "<li><a href=\"" + href + "\">" + name + 
                               (entry->d_type == DT_DIR ? "/" : "") + "</a></li>";
                    }
                }
                closedir(dir);
            }
            html += "</ul></body></html>";
            
            if (!isHead) {
                response.setBody(html);
            }
            response.setStatus(200);
            response.setHeader("Content-Type", "text/html");
            if (isHead) { std::ostringstream sizeStr; sizeStr << html.size(); response.setHeader("Content-Length", sizeStr.str()); }
        } else {
            // Directory exists but no index and autoindex is off -> Not Found
            response.setStatus(404);
            serveErrorPage(response, 404, config);
        }
    } else if (S_ISREG(st.st_mode)) {
        // Regular file handling
        std::ifstream file(resolvedPath.c_str(), std::ios::binary);
        if (file.is_open()) {
            if (!isHead) {
                std::ostringstream ss;
                ss << file.rdbuf();
                response.setBody(ss.str());
            }
            file.close();
        } else {
            // Could not open the file, even though stat succeeded
            response.setStatus(500);
            serveErrorPage(response, 500, config);
            return;
        }
        response.setStatus(200);
        response.setHeader("Content-Type", getMimeType(resolvedPath));
        if (isHead) { std::ostringstream sizeStr; sizeStr << st.st_size; response.setHeader("Content-Length", sizeStr.str()); }
    } else {
        // Niet een map of regulier bestand
        response.setStatus(403);
        serveErrorPage(response, 403, config);
    }

    // For HEAD requests, the body must be empty.
    if (isHead) {
        response.setBody("");
    }
}

// Handler for POST requests
void Server::handlePostRequest(HttpRequest& request, HttpResponse& response, 
                              const ConfigParser::ServerConfig& config, 
                              const LocationConfig& locConfig, 
                              const std::string& effectiveRoot) {
    // Check if upload processing is configured
    if (!locConfig.getUploadStore().empty()) {
        // Resolve upload directory relative to effectiveRoot even if upload_store starts with '/'
        std::string uploadStore = locConfig.getUploadStore();
        if (!uploadStore.empty() && uploadStore[0] == '/') uploadStore = uploadStore.substr(1);
        std::string uploadDir = resolvePath(effectiveRoot, uploadStore);
        if (uploadDir.empty()) {
            response.setStatus(500); // Internal Server Error
            serveErrorPage(response, 500, config);
            return;
        }

        // Check if the upload directory exists, if not, try to create it
        struct stat st;
        if (stat(uploadDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            // Try to create the upload directory
            if (mkdir(uploadDir.c_str(), 0755) != 0 && errno != EEXIST) {
                std::cerr << "Could not create upload directory: " << uploadDir << " error: " << strerror(errno) << std::endl;
                response.setStatus(500); // Interne Serverfout
                serveErrorPage(response, 500, config);
                return;
            }
        }

        // Decide how to save body: raw binary or multipart/form-data
        std::string contentType = request.getHeader("content-type");

        std::string savedFilename;
        std::string fullPath;
        const std::string& body = request.getBody();

        // Check for multipart
        std::string ctLower = toLowerAscii(contentType);
        if (ctLower.find("multipart/form-data") != std::string::npos) {
            // Extract boundary parameter robustly
            std::string boundary;
            {
                // split on ';'
                std::istringstream ss(contentType);
                std::string token;
                while (std::getline(ss, token, ';')) {
                    // trim
                    size_t f = token.find_first_not_of(" \t"); if (f != std::string::npos) token = token.substr(f); else token.clear();
                    size_t l = token.find_last_not_of(" \t"); if (l != std::string::npos) token = token.substr(0, l + 1);
                    std::string low = toLowerAscii(token);
                    if (low.find("boundary=") == 0) {
                        std::string val = token.substr(9);
                        // strip quotes
                        if (!val.empty() && (val[0] == '"' || val[0] == '\'')) { char q = val[0]; size_t q2 = val.find(q, 1); val = (q2 != std::string::npos) ? val.substr(1, q2 - 1) : val.substr(1); }
                        boundary = val; break;
                    }
                }
            }

            if (!boundary.empty()) {
                const std::string sep = std::string("--") + boundary;
                size_t searchPos = 0;
                while (true) {
                    size_t bpos = body.find(sep, searchPos);
                    if (bpos == std::string::npos) break;
                    size_t after = bpos + sep.size();
                    // Final boundary?
                    if (after + 1 < body.size() && body[after] == '-' && body[after+1] == '-') break;
                    // skip CRLF if present
                    if (after + 1 < body.size() && body[after] == '\r' && body[after+1] == '\n') after += 2;
                    size_t headersEnd = body.find("\r\n\r\n", after);
                    if (headersEnd == std::string::npos) break;
                    std::string partHeaders = body.substr(after, headersEnd - after);
                    // Parse filename
                    std::istringstream ph(partHeaders);
                    std::string hline;
                    std::string filename;
                    while (std::getline(ph, hline)) {
                        if (!hline.empty() && hline[hline.size()-1] == '\r') hline.erase(hline.size()-1);
                        std::string lower = toLowerAscii(hline);
                        if (lower.find("content-disposition:") == 0) {
                            filename = extractFilenameFromContentDisposition(hline);
                        }
                    }
                    size_t contentStart = headersEnd + 4;
                    // Find next boundary marker from contentStart
                    size_t nextMark = body.find(sep, contentStart);
                    if (nextMark == std::string::npos) break;
                    size_t contentEnd = nextMark;
                    // Exclude trailing CRLF if present
                    if (contentEnd >= 2 && body[contentEnd-2] == '\r' && body[contentEnd-1] == '\n') contentEnd -= 2;

                    if (!filename.empty()) {
                        savedFilename = filename;
                        fullPath = resolvePath(uploadDir, savedFilename);
                        if (fullPath.empty()) break;
                        std::ofstream outFile(fullPath.c_str(), std::ios::binary);
                        if (!outFile.is_open()) { fullPath.clear(); break; }
                        if (contentEnd > contentStart) outFile.write(&body[0] + contentStart, contentEnd - contentStart);
                        outFile.close();
                        break; // done
                    }
                    // advance search after this boundary
                    searchPos = nextMark + sep.size();
                }

                // As a last resort, try to sniff filename from header text in body
                if (fullPath.empty()) {
                    size_t disp = body.find("Content-Disposition:");
                    if (disp != std::string::npos) {
                        size_t lineEnd = body.find("\r\n", disp);
                        std::string headerLine = (lineEnd == std::string::npos) ? body.substr(disp) : body.substr(disp, lineEnd - disp);
                        std::string fallbackName = extractFilenameFromContentDisposition(headerLine);
                        if (!fallbackName.empty()) savedFilename = fallbackName;
                    }
                }
            }
        }

        // If not multipart or parsing failed, save raw body
        if (fullPath.empty()) {
            // Try to honor a client-provided filename via X-Filename or Content-Disposition
            std::string suggestedFilename = suggestFilenameFromHeaders(request);

            if (suggestedFilename.empty()) {
                std::ostringstream oss; oss << time(NULL);
                savedFilename = "upload_" + oss.str();
            } else {
                savedFilename = suggestedFilename;
            }
            fullPath = resolvePath(uploadDir, savedFilename);
            if (fullPath.empty()) {
                response.setStatus(500);
                serveErrorPage(response, 500, config);
                return;
            }
            std::ofstream outFile(fullPath.c_str(), std::ios::binary);
            if (!outFile.is_open()) {
                response.setStatus(500);
                serveErrorPage(response, 500, config);
                return;
            }
            outFile.write(request.getBody().c_str(), request.getBody().length());
            outFile.close();
        }

        // Success response
        response.setStatus(201);
        response.setBody("<html><body><h1>File uploaded successfully to " + fullPath + "</h1></body></html>");
        response.setHeader("Content-Type", "text/html");
        std::string requestPath = request.getPath();
        response.setHeader("Location", requestPath + (requestPath.empty() || requestPath[requestPath.length()-1] == '/' ? "" : "/") + savedFilename);
    } else {
        // POST niet geconfigureerd voor deze locatie
        response.setStatus(405); // Methode Niet Toegestaan
        response.setHeader("Allow", "GET, HEAD, OPTIONS"); // Aanpassen op basis van getAllowedMethodsForPath
        serveErrorPage(response, 405, config);
    }
}

// Handler for DELETE requests
void Server::handleDeleteRequest(HttpRequest& request, HttpResponse& response, 
                               const ConfigParser::ServerConfig& config, 
                               const LocationConfig& /*locConfig*/, // Marked as unused
                               const std::string& effectiveRoot) {
    // Los het te verwijderen bestand op
    std::string resolvedPath = resolvePath(effectiveRoot, request.getPath());
    if (resolvedPath.empty()) {
        response.setStatus(403); // Verboden
        serveErrorPage(response, 403, config);
        return;
    }

    // Check if the file exists and is a regular file
    struct stat st;
    if (stat(resolvedPath.c_str(), &st) != 0) {
        response.setStatus(404); // Not Found
        serveErrorPage(response, 404, config);
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        // Sta alleen het verwijderen van reguliere bestanden toe, geen mappen of speciale bestanden
        response.setStatus(403); // Verboden
        serveErrorPage(response, 403, config);
        return;
    }

    // Probeer het bestand te verwijderen
    if (remove(resolvedPath.c_str()) == 0) { // remove is in cstdio of unistd.h
        response.setStatus(200); // OK
        response.setBody("<html><body><h1>File deleted successfully</h1></body></html>");
        response.setHeader("Content-Type", "text/html");
    } else {
        // Fout bij verwijderen (machtigingen, enz.)
        response.setStatus(500); // Interne Serverfout
        serveErrorPage(response, 500, config);
    }
}

// Handler for OPTIONS requests
void Server::handleOptionsRequest(HttpRequest& request, HttpResponse& response, 
                                 const ConfigParser::ServerConfig& config) {
    // Verkrijg toegestane methoden voor dit pad
    std::set<std::string> allowedMethods = getAllowedMethodsForPath(request.getPath(), config);
    
    // Bouw Allow-header
    std::string allowHeaderVal;
    for (std::set<std::string>::const_iterator it = allowedMethods.begin(); it != allowedMethods.end(); ++it) {
        if (!allowHeaderVal.empty()) allowHeaderVal += ", ";
        allowHeaderVal += *it;
    }
    
    // Zorg ervoor dat OPTIONS is opgenomen in de toegestane methoden
    if (allowHeaderVal.find("OPTIONS") == std::string::npos) {
        if (!allowHeaderVal.empty()) allowHeaderVal += ", ";
        allowHeaderVal += "OPTIONS";
    }

    // Stel respons in
    response.setStatus(200); // OK
    response.setHeader("Allow", allowHeaderVal);
    response.setHeader("Content-Length", "0"); // OPTIONS heeft geen body
    response.setBody("");
}

// Handler for PUT requests
void Server::handlePutRequest(HttpRequest& request, HttpResponse& response,
                              const ConfigParser::ServerConfig& config,
                              const LocationConfig& locConfig,
                              const std::string& effectiveRoot) {
    // Must be allowed by allow_methods PUT
    // Save file to upload_store (or fallback to effectiveRoot)
    // Resolve upload_store relative to effectiveRoot even if it starts with '/'
    std::string targetDir;
    if (locConfig.getUploadStore().empty()) {
        targetDir = effectiveRoot;
    } else {
        std::string uploadStore = locConfig.getUploadStore();
        if (!uploadStore.empty() && uploadStore[0] == '/') uploadStore = uploadStore.substr(1);
        targetDir = resolvePath(effectiveRoot, uploadStore);
    }
    if (targetDir.empty()) {
        response.setStatus(500);
        serveErrorPage(response, 500, config);
        return;
    }

    struct stat st;
    if (stat(targetDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        if (mkdir(targetDir.c_str(), 0755) != 0 && errno != EEXIST) {
            response.setStatus(500);
            serveErrorPage(response, 500, config);
            return;
        }
    }

    // Derive suggested filename from headers (X-Filename or Content-Disposition)
    std::string suggestedFilename = suggestFilenameFromHeaders(request);

    // Determine subpath (directory or filename) from URI after location prefix
    std::string uriPath = request.getPath();
    std::string locPath = locConfig.getPath();
    std::string relativeSubpath;
    if (!locPath.empty() && uriPath.find(locPath) == 0) {
        relativeSubpath = uriPath.substr(locPath.size());
        if (!relativeSubpath.empty() && relativeSubpath[0] == '/') relativeSubpath = relativeSubpath.substr(1);
    } else {
        // fallback to last segment
        size_t slash = uriPath.find_last_of('/');
        relativeSubpath = (slash == std::string::npos) ? uriPath : uriPath.substr(slash + 1);
    }

    std::string finalPath;
    if (relativeSubpath.empty()) {
        // No name provided in URL; use suggested or generate
        std::string nameToUse = suggestedFilename;
        if (nameToUse.empty()) { std::ostringstream oss; oss << "put_" << time(NULL); nameToUse = oss.str(); }
        finalPath = resolvePath(targetDir, nameToUse);
    } else {
        // If subpath looks like a directory (no dot in last segment), and we have a suggested filename, store inside that directory
        size_t lastSlash = relativeSubpath.find_last_of('/');
        std::string lastSegment = (lastSlash == std::string::npos) ? relativeSubpath : relativeSubpath.substr(lastSlash + 1);
        bool treatAsDirectory = (lastSegment.find('.') == std::string::npos) && !suggestedFilename.empty();
        if (treatAsDirectory) {
            std::string dirResolved = resolvePath(targetDir, relativeSubpath);
            if (dirResolved.empty()) { response.setStatus(403); serveErrorPage(response, 403, config); return; }
            // Create directory path if needed
            if (!createDirectoriesRecursively(dirResolved)) { response.setStatus(500); serveErrorPage(response, 500, config); return; }
            finalPath = resolvePath(dirResolved, suggestedFilename);
        } else {
            // Treat as explicit filename (may include nested directories)
            finalPath = resolvePath(targetDir, relativeSubpath);
            if (finalPath.empty()) { response.setStatus(403); serveErrorPage(response, 403, config); return; }
            // Ensure parent directories exist
            size_t ps = finalPath.find_last_of('/');
            if (ps != std::string::npos) {
                std::string parent = finalPath.substr(0, ps);
                if (!createDirectoriesRecursively(parent)) { response.setStatus(500); serveErrorPage(response, 500, config); return; }
            }
        }
    }

    std::string fullPath = finalPath;
    if (fullPath.empty()) {
        response.setStatus(403);
        serveErrorPage(response, 403, config);
        return;
    }

    // Enforce max body size
    if (currentConfig.clientMaxBodySize > 0 && (long)request.getBody().size() > currentConfig.clientMaxBodySize) {
        response.setStatus(413);
        serveErrorPage(response, 413, config);
        return;
    }

    std::ofstream out(fullPath.c_str(), std::ios::binary);
    if (!out.is_open()) {
        response.setStatus(500);
        serveErrorPage(response, 500, config);
        return;
    }
    out.write(request.getBody().c_str(), request.getBody().size());
    out.close();

    response.setStatus(201); // Created
    response.setHeader("Content-Type", "text/plain");
    response.setBody("Created: " + fullPath);
}
