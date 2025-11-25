#include <iostream>
#include "Server.hpp"
#include <string>
#include <cstdlib>

int main(int argc, char* argv[]) {
    std::string configFilePath = "config/default.conf"; // Default config file

    if (argc > 1) {
        configFilePath = argv[1]; // Use command line argument if provided
    }

    try {
        // Initialize the server with the configuration file path
        Server server(configFilePath);

        std::cout << "Attempting to start server with config: " << configFilePath << std::endl;
        // Start the server
        server.start(); 

        std::cout << "Server has been instructed to start." << std::endl;
        std::cout << "To stop the server, you might need to send a signal (e.g., Ctrl+C) "
                  << "if it's running in the foreground." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Server initialization or runtime error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
