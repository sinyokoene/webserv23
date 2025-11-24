#include <iostream>
#include "Server.hpp"
#include <string> // Required for std::string
#include <cstdlib> // Required for EXIT_FAILURE and EXIT_SUCCESS

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
        // The start() method is void, so we can't check its return value directly here.
        // We assume if it doesn't throw, it's on its way to starting.
        // Actual listening and client handling logic would be within Server::start() or methods it calls.
        server.start(); 

        // If start() is meant to be blocking and run the server loop,
        // then the program will effectively run here.
        // If start() is non-blocking, you might need a loop here or another mechanism
        // to keep the main thread alive while the server runs (e.g., join threads if it's multi-threaded).
        // For now, we'll assume start() handles the server's operational loop or sets up
        // necessary background tasks.
        std::cout << "Server has been instructed to start." << std::endl;
        std::cout << "To stop the server, you might need to send a signal (e.g., Ctrl+C) "
                  << "if it's running in the foreground, or implement a specific stop mechanism." << std::endl;
        
        // Placeholder for keeping the main thread alive if Server::start() is non-blocking
        // This could be a more sophisticated loop or condition based on server state
        // For example, if Server had an isRunning() method:
        // while(server.isRunning()) { std::this_thread::sleep_for(std::chrono::seconds(1)); }
        // For now, we'll just let main finish. If start() spawns threads, they need to be managed.
        // If start() is blocking, the lines after server.start() won't execute until it returns.

    } catch (const std::exception& e) {
        std::cerr << "Server initialization or runtime error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}