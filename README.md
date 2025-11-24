# Web Server Project

This project is a simple web server implementation designed to handle HTTP requests and serve static content. It includes features such as configuration file parsing, handling of HTTP requests and responses, and support for CGI scripts.

## Project Structure

- **Makefile**: Contains build instructions for compiling the project and generating the executable.
- **README.md**: Overview of the project and usage instructions.
- **config/**: Directory containing configuration files for the web server.
  - `default.conf`: Default settings for the web server.
  - `server1.conf`: Configuration for server instance 1.
  - `server2.conf`: Configuration for server instance 2.
- **www/**: Directory for the sample static site.
  - `index.html`: Main HTML file serving as the landing page.
  - `images/`: Contains image files used in the static site.
  - `errors/`: Contains error pages, including `404.html` for not found errors.
- **uploads/**: Directory for storing uploaded files.
- **cgi-bin/**: Directory for CGI scripts.
  - `test.php`: Example PHP script demonstrating server-side scripting.
- **include/**: Directory for header files.
  - Contains declarations for classes handling configuration, HTTP requests, responses, server operations, and utility functions.
- **src/**: Directory for source files.
  - Contains implementations for the main application and various classes.

## Usage

1. Clone the repository.
2. Navigate to the project directory.
3. Run `make` to build the project.
4. Configure the server by editing the configuration files in the `config/` directory.
5. Start the server and access it via a web browser.

## License

This project is licensed under the MIT License. See the LICENSE file for more details.