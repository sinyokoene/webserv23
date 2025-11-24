#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdexcept>
#include <string>

class Socket {
public:
    Socket(int domain, int type, int protocol);
    ~Socket();

    void bind(const sockaddr_in& address);
    void listen(int backlog);
    int accept(sockaddr_in& clientAddress);
    void connect(const sockaddr_in& address);
    void send(const std::string& data);
    std::string receive(size_t bufferSize);

private:
    int sockfd;
};

#endif // SOCKET_HPP