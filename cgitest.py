#!/usr/bin/env python3
"""
Test de CGI para webserv: comprueba que se ejecutan un .py y un .sh.

Uso (desde /home/mikel/Desktop/github, con el servidor ya arrancado):
    python3 cgitest.py
    python3 cgitest.py --host localhost --port 8321 --conf confile.conf --prefix /cgi-bin
"""
import argparse
import http.client
import os
import re
import socket
import stat
import sys

PY_CGI = r'''#!/usr/bin/env python3
import os, sys
n = int(os.environ.get("CONTENT_LENGTH") or 0)
body = sys.stdin.read(n) if n > 0 else ""
sys.stdout.write("Content-Type: text/plain\r\n\r\n")
print("INTERPRETE=python")
print("METHOD=" + os.environ.get("REQUEST_METHOD", ""))
print("QUERY=" + os.environ.get("QUERY_STRING", ""))
print("BODY=" + body)
'''

SH_CGI = r'''#!/bin/sh
printf 'Content-Type: text/plain\r\n\r\n'
echo "INTERPRETE=sh"
echo "METHOD=$REQUEST_METHOD"
echo "QUERY=$QUERY_STRING"
if [ "${CONTENT_LENGTH:-0}" -gt 0 ] 2>/dev/null; then
    printf 'BODY='; head -c "$CONTENT_LENGTH"; echo
else
    echo "BODY="
fi
'''

GREEN, RED, RESET = "\033[32m", "\033[31m", "\033[0m"
results = []


def write_cgi(path, content):
    with open(path, "w", newline="\n") as f:
        f.write(content)
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def cgi_dir_from_conf(conf, prefix):
    """Devuelve el root/alias del location `prefix` en el fichero de config."""
    with open(conf) as f:
        text = re.sub(r"#[^\n]*", "", f.read())
    m = re.search(r"location\s+" + re.escape(prefix) + r"\s*\{([^}]*)\}", text)
    if not m:
        sys.exit(f"No hay 'location {prefix}' en {conf}")
    d = re.search(r"\b(?:alias|root)\s+([^;\s]+)\s*;", m.group(1))
    if not d:
        sys.exit(f"'location {prefix}' en {conf} no tiene root ni alias")
    return d.group(1)


def request(host, port, method, url, body=None, headers=None):
    conn = http.client.HTTPConnection(host, port, timeout=5)
    try:
        conn.request(method, url, body=body, headers=headers or {})
        r = conn.getresponse()
        return r.status, r.read().decode(errors="replace")
    finally:
        conn.close()


def request_chunked(host, port, url, body):
    """POST chunked a mano con socket, para no depender de http.client."""
    chunks = b""
    for i in range(0, len(body), 4):
        part = body[i:i + 4]
        chunks += b"%x\r\n" % len(part) + part + b"\r\n"
    chunks += b"0\r\n\r\n"
    req = (f"POST {url} HTTP/1.1\r\nHost: {host}:{port}\r\n"
           "Content-Type: text/plain\r\nTransfer-Encoding: chunked\r\n"
           "Connection: close\r\n\r\n").encode() + chunks
    s = socket.create_connection((host, port), timeout=5)
    try:
        s.sendall(req)
        data = b""
        while True:
            b = s.recv(4096)
            if not b:
                break
            data += b
    finally:
        s.close()
    text = data.decode(errors="replace")
    try:
        status = int(text.split(" ", 2)[1])
    except (IndexError, ValueError):
        status = 0
    return status, text.split("\r\n\r\n", 1)[-1]


def check(name, fn, expect_status, expect_in):
    try:
        status, body = fn()
    except Exception as e:
        results.append(False)
        print(f"{RED}FAIL{RESET} {name:<28} excepción: {e}")
        return
    missing = [s for s in expect_in if s not in body]
    ok = status == expect_status and not missing
    results.append(ok)
    tag = f"{GREEN}OK  {RESET}" if ok else f"{RED}FAIL{RESET}"
    print(f"{tag} {name:<28} status={status}")
    if not ok:
        if status != expect_status:
            print(f"       esperado status {expect_status}")
        if missing:
            print(f"       falta en la respuesta: {missing}")
        print("       respuesta:", body.strip()[:300].replace("\n", "\n                  "))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="localhost")
    ap.add_argument("--port", type=int, default=8321)
    ap.add_argument("--conf", default="confile.conf", help="config del servidor")
    ap.add_argument("--dir", help="carpeta donde crear los CGI (por defecto, la del location en --conf)")
    ap.add_argument("--prefix", default="/cgi-bin", help="ruta URL del location")
    a = ap.parse_args()
    if not a.dir:
        a.dir = cgi_dir_from_conf(a.conf, a.prefix)

    os.makedirs(a.dir, exist_ok=True)
    write_cgi(os.path.join(a.dir, "test.py"), PY_CGI)
    write_cgi(os.path.join(a.dir, "test.sh"), SH_CGI)
    print(f"CGIs creados en {os.path.abspath(a.dir)}\n")

    h, p = a.host, a.port
    for ext, interp in ((".py", "python"), (".sh", "sh")):
        url = f"{a.prefix}/test{ext}"
        print(f"--- {url} ---")
        check(f"GET  {ext} con query",
              lambda: request(h, p, "GET", url + "?a=1&b=2"),
              200, [f"INTERPRETE={interp}", "METHOD=GET", "QUERY=a=1&b=2"])
        check(f"POST {ext} urlencoded",
              lambda: request(h, p, "POST", url, "nombre=mikol&x=42",
                              {"Content-Type": "application/x-www-form-urlencoded"}),
              200, [f"INTERPRETE={interp}", "METHOD=POST", "BODY=nombre=mikol&x=42"])
        check(f"POST {ext} chunked",
              lambda: request_chunked(h, p, url, b"hola-desde-chunked"),
              200, [f"INTERPRETE={interp}", "BODY=hola-desde-chunked"])
        print()

    check("GET  script inexistente",
          lambda: request(h, p, "GET", f"{a.prefix}/noexiste.py"), 404, [])

    ok = sum(results)
    color = GREEN if ok == len(results) else RED
    print(f"\n{color}{ok}/{len(results)} tests OK{RESET}")
    sys.exit(0 if ok == len(results) else 1)


if __name__ == "__main__":
    main()