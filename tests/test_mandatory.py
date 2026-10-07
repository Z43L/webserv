#!/usr/bin/env python3
"""
Tests de la parte obligatoria del subject de webserv.

Uso (desde la raiz del repo, con el servidor arrancado con confile.conf):
    ./webserv confile.conf &
    python3 tests/test_mandatory.py
    python3 tests/test_mandatory.py --slow     # incluye el timeout del CGI (~30 s)
"""
import argparse
import hashlib
import http.client
import os
import socket
import stat
import sys
import threading
import time

HOST = "localhost"
PORT = 8321
PORT2 = 8322
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CGI_DIR = os.path.join(ROOT, "web", "cgi_bin")
UPLOAD_DIR = os.path.join(ROOT, "web", "uploads")

GREEN, RED, RESET = "\033[32m", "\033[31m", "\033[0m"
results = []

CGI_SCRIPTS = {
    "test.py": r'''#!/usr/bin/env python3
import os, sys
n = int(os.environ.get("CONTENT_LENGTH") or 0)
body = sys.stdin.read(n) if n > 0 else ""
sys.stdout.write("Content-Type: text/plain\r\n\r\n")
print("METHOD=" + os.environ.get("REQUEST_METHOD", ""))
print("QUERY=" + os.environ.get("QUERY_STRING", ""))
print("BODY=" + body)
''',
    "slow.py": '''#!/usr/bin/env python3
import time
time.sleep(3)
print("Content-Type: text/plain\\r\\n\\r\\nlento")
''',
    "loop.py": '''#!/usr/bin/env python3
while True:
    pass
''',
    "silent.py": '''#!/usr/bin/env python3
import sys
sys.exit(1)
''',
    "relpath.py": '''#!/usr/bin/env python3
print("Content-Type: text/plain\\r\\n\\r\\n" + open("datos.txt").read())
''',
    "status.sh": '''#!/bin/sh
printf 'Status: 201 Created\\r\\nContent-Type: text/plain\\r\\nX-Cgi: si\\r\\n\\r\\ncreado'
''',
}


def setup():
    os.makedirs(CGI_DIR, exist_ok=True)
    os.makedirs(UPLOAD_DIR, exist_ok=True)
    for name, content in CGI_SCRIPTS.items():
        path = os.path.join(CGI_DIR, name)
        with open(path, "w", newline="\n") as f:
            f.write(content)
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    with open(os.path.join(CGI_DIR, "datos.txt"), "w") as f:
        f.write("leido-con-ruta-relativa")


def request(method, url, body=None, headers=None, port=PORT, timeout=10):
    conn = http.client.HTTPConnection(HOST, port, timeout=timeout)
    try:
        conn.request(method, url, body=body, headers=headers or {})
        r = conn.getresponse()
        return r.status, dict((k.lower(), v) for k, v in r.getheaders()), r.read()
    finally:
        conn.close()


def raw(data, port=PORT, timeout=10):
    """Envia bytes tal cual y devuelve (status, respuesta completa)."""
    s = socket.create_connection((HOST, port), timeout=timeout)
    try:
        s.sendall(data)
        out = b""
        while True:
            b = s.recv(65536)
            if not b:
                break
            out += b
    finally:
        s.close()
    try:
        status = int(out.split(b" ", 2)[1])
    except (IndexError, ValueError):
        status = 0
    return status, out


def check(name, cond, detail=""):
    results.append(bool(cond))
    tag = f"{GREEN}OK  {RESET}" if cond else f"{RED}FAIL{RESET}"
    print(f"{tag} {name}" + ("" if cond else f"\n       {detail}"))


def section(title):
    print(f"\n--- {title} ---")


def test_static():
    section("Web estatica y errores")
    st, h, b = request("GET", "/")
    check("GET / -> 200 html", st == 200 and "text/html" in h.get("content-type", ""), (st, h))
    st, h, b = request("GET", "/noexiste.html")
    check("GET inexistente -> 404 con error_page configurada", st == 404 and b"404" in b and len(b) > 0, st)
    st, h, b = request("POST", "/", body="x")
    check("POST en / (solo GET) -> 405 con Allow", st == 405 and h.get("allow") == "GET", (st, h))
    check("405 usa pagina de error por defecto", b"405" in b, b[:80])
    st, h, b = request("GET", "/old")
    check("return 301 -> Location", st == 301 and h.get("location") == "/index.html", (st, h))
    st, _, b = request("GET", "/directory/")
    check("index de location", st == 200 and b.strip() == b"youpi.bad_extension", (st, b[:60]))
    st, _, b = request("GET", "/uploads/")
    check("autoindex on", st == 200 and b"Index of /uploads/" in b, (st, b[:80]))
    st, _, _ = request("GET", "/directory/Yeah")
    check("directorio sin index ni autoindex -> 404", st == 404, st)
    st, _ = raw(b"GET /../../../../etc/passwd HTTP/1.1\r\nHost: x\r\n\r\n")
    check("path traversal no sirve /etc/passwd", st in (400, 403, 404), st)


def test_multiport():
    section("Varios puertos")
    _, _, b1 = request("GET", "/", port=PORT)
    st, _, b2 = request("GET", "/", port=PORT2)
    check(f"puerto {PORT2} responde", st == 200, st)
    check("cada puerto sirve contenido distinto", b1 != b2 and b"Segundo servidor" in b2, b2[:80])


def test_upload_delete():
    section("Subida de archivos y DELETE")
    data = os.urandom(10 * 1024 * 1024)
    st, _, _ = request("POST", "/uploads/binario.bin", body=data,
                       headers={"Content-Type": "application/octet-stream"})
    check("POST binario 10MB -> 201", st == 201, st)
    st, _, b = request("GET", "/uploads/binario.bin")
    check("el archivo subido es identico", st == 200 and hashlib.md5(b).digest() == hashlib.md5(data).digest(),
          (st, len(b)))

    boundary = "----webservtest"
    content = b"contenido\r\ncon lineas\r\n--y guiones"
    body = (f"--{boundary}\r\n"
            'Content-Disposition: form-data; name="campo"\r\n\r\n'
            "valor\r\n"
            f"--{boundary}\r\n"
            'Content-Disposition: form-data; name="file"; filename="formulario.txt"\r\n'
            "Content-Type: text/plain\r\n\r\n").encode() + content + f"\r\n--{boundary}--\r\n".encode()
    st, _, _ = request("POST", "/uploads", body=body,
                       headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
    check("POST multipart (formulario del navegador) -> 201", st == 201, st)
    st, _, b = request("GET", "/uploads/formulario.txt")
    check("multipart guarda solo el contenido del archivo", st == 200 and b == content, b[:80])

    st, _, _ = request("DELETE", "/uploads/formulario.txt")
    check("DELETE -> 204", st == 204, st)
    st, _, _ = request("GET", "/uploads/formulario.txt")
    check("tras DELETE -> 404", st == 404, st)
    st, _, _ = request("DELETE", "/uploads/formulario.txt")
    check("DELETE de algo que no existe -> 404", st == 404, st)
    st, _, _ = request("DELETE", "/index.html")
    check("DELETE donde no esta permitido -> 405", st == 405, st)
    request("DELETE", "/uploads/binario.bin")


def test_body_limit():
    section("client_max_body_size")
    st, _, _ = request("POST", "/post_body", body="a" * 100)
    check("100 bytes (limite) -> 200", st == 200, st)
    st, _, _ = request("POST", "/post_body", body="a" * 101)
    check("101 bytes -> 413", st == 413, st)
    chunks = b"".join(b"%x\r\n" % 50 + b"b" * 50 + b"\r\n" for _ in range(4)) + b"0\r\n\r\n"
    st, _ = raw(b"POST /post_body HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n" + chunks)
    check("chunked de 200 bytes -> 413", st == 413, st)


def test_cgi():
    section("CGI")
    st, _, b = request("GET", "/cgi-bin/test.py?a=1&b=2")
    check("GET con query string", st == 200 and b"QUERY=a=1&b=2" in b and b"METHOD=GET" in b, b[:120])
    st, _, b = request("POST", "/cgi-bin/test.py", body="hola=mundo",
                       headers={"Content-Type": "application/x-www-form-urlencoded"})
    check("POST pasa el body por stdin", st == 200 and b"BODY=hola=mundo" in b, b[:120])
    chunks = b"5\r\nhola-\r\n7\r\nchunked\r\n0\r\n\r\n"
    st, out = raw(b"POST /cgi-bin/test.py HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n" + chunks)
    check("POST chunked llega des-chunkeado", st == 200 and b"BODY=hola-chunked" in out, out[-120:])
    st, h, b = request("GET", "/cgi-bin/status.sh")
    check("cabeceras Status y propias del CGI", st == 201 and h.get("x-cgi") == "si" and b == b"creado", (st, h, b))
    st, _, b = request("GET", "/cgi-bin/relpath.py")
    check("el CGI se ejecuta en su directorio", st == 200 and b"leido-con-ruta-relativa" in b, b[:120])
    st, _, _ = request("GET", "/cgi-bin/silent.py")
    check("CGI sin salida -> 502", st == 502, st)
    st, _, _ = request("GET", "/cgi-bin/noexiste.py")
    check("CGI inexistente -> 404", st == 404, st)

    # Mientras un CGI tarda 3 s, el servidor tiene que seguir atendiendo
    slow = {}
    t = threading.Thread(target=lambda: slow.update(r=request("GET", "/cgi-bin/slow.py")))
    t.start()
    time.sleep(0.3)
    t0 = time.time()
    st, _, _ = request("GET", "/")
    elapsed = time.time() - t0
    t.join()
    check("un CGI lento no bloquea al resto", st == 200 and elapsed < 1, f"GET / tardo {elapsed:.2f}s")
    check("el CGI lento termina bien", slow["r"][0] == 200 and slow["r"][2].strip() == b"lento", slow["r"][:1])


def test_cgi_timeout():
    section("Timeout de CGI (lento)")
    t0 = time.time()
    st, _, _ = request("GET", "/cgi-bin/loop.py", timeout=60)
    check("CGI en bucle infinito -> 504", st == 504, f"{st} en {time.time() - t0:.1f}s")


def test_malformed():
    section("Peticiones mal formadas")
    st, _ = raw(b"BASURA\r\n\r\n")
    check("linea de peticion invalida -> 400", st == 400, st)
    st, _ = raw(b"GET / HTTP/9.9\r\nHost: x\r\n\r\n")
    check("version no soportada -> 505", st == 505, st)
    st, _ = raw(b"PATCH / HTTP/1.1\r\nHost: x\r\n\r\n")
    check("metodo desconocido -> 501", st == 501, st)
    st, _ = raw(b"GET / HTTP/1.1\r\nX-Grande: " + b"a" * 20000 + b"\r\n\r\n")
    check("cabeceras enormes -> 431", st == 431, st)
    st, out = raw(b"GET /cgi-bin/test.py?x=%20y HTTP/1.1\r\nHost: x\r\n\r\n")
    check("query con %20 llega intacta al CGI", st == 200 and b"QUERY=x=%20y" in out, out[-80:])


def test_resilience():
    section("Resiliencia")
    for _ in range(20):
        s = socket.create_connection((HOST, PORT))
        s.sendall(b"POST /uploads/x HTTP/1.1\r\nContent-Length: 1000\r\n\r\nmedio")
        s.close()
    s = socket.create_connection((HOST, PORT))
    s.close()
    st, _, _ = request("GET", "/")
    check("clientes que cortan a mitad no tumban el servidor", st == 200, st)

    errors = []

    def worker():
        for _ in range(20):
            try:
                if request("GET", "/")[0] != 200:
                    errors.append("status")
            except Exception as e:
                errors.append(str(e))

    threads = [threading.Thread(target=worker) for _ in range(50)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    check("50 clientes x 20 GET concurrentes", not errors, errors[:3])

    cgi_errors = []

    def cgi_worker(i):
        try:
            st, _, b = request("POST", "/cgi-bin/test.py", body=f"n={i}", timeout=30)
            if st != 200 or f"BODY=n={i}".encode() not in b:
                cgi_errors.append((st, b[:60]))
        except Exception as e:
            cgi_errors.append(str(e))

    threads = [threading.Thread(target=cgi_worker, args=(i,)) for i in range(30)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    check("30 CGI concurrentes con su propia respuesta", not cgi_errors, cgi_errors[:3])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--slow", action="store_true", help="incluye el timeout del CGI")
    a = ap.parse_args()
    setup()
    try:
        socket.create_connection((HOST, PORT), timeout=2).close()
    except OSError:
        sys.exit(f"No hay servidor en {HOST}:{PORT}. Arrancalo con ./webserv confile.conf")

    test_static()
    test_multiport()
    test_upload_delete()
    test_body_limit()
    test_cgi()
    test_malformed()
    test_resilience()
    if a.slow:
        test_cgi_timeout()

    st, _, _ = request("GET", "/")
    check("el servidor sigue vivo al final", st == 200, st)
    ok = sum(results)
    color = GREEN if ok == len(results) else RED
    print(f"\n{color}{ok}/{len(results)} tests OK{RESET}")
    sys.exit(0 if ok == len(results) else 1)


if __name__ == "__main__":
    main()
