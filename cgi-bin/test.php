<?php
// This is a simple CGI script that demonstrates server-side scripting capabilities.

// Set the content type to HTML
header("Content-Type: text/html");

// Start the output
echo "<!DOCTYPE html>";
echo "<html lang='en'>";
echo "<head>";
echo "<meta charset='UTF-8'>";
echo "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
echo "<title>Test CGI Script</title>";
echo "</head>";
echo "<body>";
echo "<h1>Hello from test.php!</h1>";
echo "<p>This is a simple CGI script running on the server.</p>";
echo "</body>";
echo "</html>";
?>