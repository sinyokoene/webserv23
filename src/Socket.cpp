#include "Socket.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>

Socket::Socket(int domain, int type, int protocol) {
    sockfd = socket(domain, type, protocol);
    if (sockfd < 0) {
        throw std::runtime_error("Failed to create socket");
    }
}

Socket::~Socket() {
    close(sockfd);
}

void Socket::bind(const sockaddr_in& address) {
    if (::bind(sockfd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        throw std::runtime_error("Failed to bind socket");
    }
}

void Socket::listen(int backlog) {
    if (::listen(sockfd, backlog) < 0) {
        throw std::runtime_error("Failed to listen on socket");
    }
}

int Socket::accept(sockaddr_in& clientAddress) {
    socklen_t clientLength = sizeof(clientAddress);
    int clientSocket = ::accept(sockfd, (struct sockaddr *)&clientAddress, &clientLength);
    if (clientSocket < 0) {
        throw std::runtime_error("Failed to accept connection");
    }
    return clientSocket;
}

void Socket::connect(const sockaddr_in& address) {
    if (::connect(sockfd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        throw std::runtime_error("Failed to connect to socket");
    }
}

void Socket::send(const std::string& data) {
    if (::send(sockfd, data.c_str(), data.length(), 0) < 0) {
        throw std::runtime_error("Failed to send data");
    }
}

std::string Socket::receive(size_t bufferSize) {
    char* buffer = new char[bufferSize];
    std::memset(buffer, 0, bufferSize);
    
    ssize_t bytesReceived = ::recv(sockfd, buffer, bufferSize - 1, 0);
    if (bytesReceived < 0) {
        delete[] buffer;
        throw std::runtime_error("Failed to receive data");
    }
    
    std::string result(buffer, bytesReceived);
    delete[] buffer;
    return result;
}