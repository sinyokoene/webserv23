#!/bin/sh
echo "Content-Type: text/plain"
echo ""
echo "CGI OK"
echo "REQUEST_METHOD: $REQUEST_METHOD"
echo "CONTENT_TYPE: $CONTENT_TYPE"
echo "CONTENT_LENGTH: $CONTENT_LENGTH"
echo "Query String: $QUERY_STRING"
# Attempt to read from stdin to see if POST data is coming through
# This is a simple way, a real script would parse based on CONTENT_LENGTH
if [ "$REQUEST_METHOD" = "POST" ]; then
    echo "POST Data (first 100 chars):"
    head -c 100 || true # Read up to 100 chars from stdin
    echo ""
fi
