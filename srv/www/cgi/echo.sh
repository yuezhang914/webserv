#!/bin/sh

BODY=$(cat)

printf 'Content-Type: text/plain\r\n'
printf '\r\n'
printf 'CGI=SHELL\n'
printf 'METHOD=%s\n' "$REQUEST_METHOD"
printf 'QUERY=%s\n' "$QUERY_STRING"
printf 'BODY=%s\n' "$BODY"
