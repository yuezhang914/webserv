#!/bin/bash
printf "Content-Type: text/plain\r\n\r\n"
echo "Shell CGI Execution Test"
echo "REQUEST_METHOD: $REQUEST_METHOD"
echo "QUERY_STRING: $QUERY_STRING"