#!/usr/bin/env python3
import os, sys
n = int(os.environ.get("CONTENT_LENGTH") or 0)
body = sys.stdin.read(n) if n > 0 else ""
sys.stdout.write("Content-Type: text/plain\r\n\r\n")
print("INTERPRETE=python")
print("METHOD=" + os.environ.get("REQUEST_METHOD", ""))
print("QUERY=" + os.environ.get("QUERY_STRING", ""))
print("BODY=" + body)
