#!/bin/sh
printf 'Content-Type: text/plain\r\n\r\n'
echo "INTERPRETE=sh"
echo "METHOD=$REQUEST_METHOD"
echo "QUERY=$QUERY_STRING"
if [ "${CONTENT_LENGTH:-0}" -gt 0 ] 2>/dev/null; then
    printf 'BODY='; head -c "$CONTENT_LENGTH"; echo
else
    echo "BODY="
fi
