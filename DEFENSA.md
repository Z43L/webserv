# Webserv — guía para la corrección

## Arrancar

```sh
make
./webserv confile.conf        # sin argumento usa confile.conf
```

Escucha en **8321** (web principal, CGI, uploads) y **8322** (segundo servidor, `web2/`).

## Arquitectura en 1 minuto

- **Un único `epoll`** (`Socket::runServers`) para todo: sockets de escucha de todos los `server`, clientes y tuberías de los CGI.
- Ningún `read`/`recv`/`write`/`send` sobre sockets o pipes sin que `epoll` haya avisado antes. No se mira `errno` después de leer/escribir: si `recv`/`send` devuelve `<= 0`, se cierra el cliente.
- Cada cliente tiene un `ClientSession`: buffer de lectura, respuesta pendiente (con `write_offset`) y, si toca, el estado de su CGI.
- Flujo: `accept` → leer hasta tener la petición completa (`Content-Length` o `chunked`) → `routeRequest` → respuesta → enviar → cerrar (`Connection: close`).
- Cada segundo se revisan los timeouts: cliente inactivo → **408**, CGI que tarda más de 30 s → `kill` + **504**, y se recogen los hijos con `waitpid(WNOHANG)` (sin zombis).

## CGI (sin bloquear)

1. `Parsercgi::start` crea dos pipes, hace `fork`; el hijo hace `dup2` a stdin/stdout, `chdir` al directorio del script (rutas relativas) y `execve(interprete, script)` con las variables CGI (`REQUEST_METHOD`, `QUERY_STRING`, `CONTENT_LENGTH`, `CONTENT_TYPE`, `PATH_INFO`, `HTTP_*`...).
2. El padre mete los pipes en el `epoll`: escribe el body (ya des-chunkeado) cuando el pipe acepta datos y lee la salida cuando hay datos.
3. Al llegar EOF se construye la respuesta con las cabeceras del CGI (`Status`, `Content-Type`...). Sin salida → **502**.
4. `fork` solo se usa para el CGI.

## Configuración (`confile.conf`)

| Directiva | Dónde | Qué hace |
|---|---|---|
| `listen 8321` / `listen ip:puerto` | server | puerto (un bloque `server` por puerto) |
| `root`, `index` | server / location | directorio y archivo por defecto |
| `error_page 404 /404.html` | server | página de error propia (si no hay, se genera una) |
| `client_max_body_size` | server / location | límite de body → **413** |
| `client_read_timeout` | server | segundos sin actividad → **408** |
| `allow_methods GET POST DELETE` | location | métodos aceptados → si no, **405** con `Allow` |
| `return 301 /url` | location | redirección |
| `alias` | location | sustituye el prefijo de la ruta |
| `autoindex on` | location | listado de directorio |
| `upload_store dir` | location | dónde se guardan los POST (incluye `multipart/form-data`) |
| `cgi_ext .py .sh` + `cgi_path /usr/bin/python3 /bin/bash` | location | CGI por extensión (el intérprete n-ésimo va con la extensión n-ésima) |

## Demostración rápida

```sh
# Web estática y errores
curl -i localhost:8321/                       # 200
curl -i localhost:8321/noexiste               # 404 con web/404.html
curl -i -X POST localhost:8321/               # 405 (/ solo acepta GET)
curl -i localhost:8321/old                    # 301 -> /index.html
curl -i localhost:8321/uploads/               # autoindex

# Varios puertos, contenido distinto
curl localhost:8322/

# Subir, ver y borrar
curl -i -X POST --data-binary @README.md localhost:8321/uploads/readme.txt   # 201
curl -i -F "file=@makefile" localhost:8321/uploads                          # 201 (multipart)
curl localhost:8321/uploads/readme.txt
curl -i -X DELETE localhost:8321/uploads/readme.txt                         # 204

# Límite de body
curl -i -X POST -d "$(printf 'a%.0s' $(seq 101))" localhost:8321/post_body   # 413

# CGI (Python y Bash)
curl -i "localhost:8321/cgi-bin/test.py?a=1&b=2"
curl -i -X POST -d "hola=mundo" localhost:8321/cgi-bin/test.py
curl -i -H "Transfer-Encoding: chunked" -d "chunked!" localhost:8321/cgi-bin/test.sh

# Peticiones a mano
printf 'GET / HTTP/9.9\r\n\r\n' | nc localhost 8321            # 505
printf 'BASURA\r\n\r\n' | nc localhost 8321                    # 400

# Estrés
siege -b -c 50 -t 30s http://localhost:8321/                    # disponibilidad ~100 %
```

En el navegador: `http://localhost:8321/` y `http://localhost:8321/uploads/`.

## Tests

```sh
python3 tests/test_mandatory.py           # 40 tests de la parte obligatoria
python3 tests/test_mandatory.py --slow    # + timeout de CGI (~30 s)
python3 cgitest.py                        # CGI .py y .sh
./tester http://localhost:8321            # tester de 42 (usa YoupiBanane/ y cgi_tester)
```

## Preguntas típicas

- **¿Por qué `epoll` y no `poll`?** El subject permite cualquier equivalente; `epoll` escala mejor con muchos clientes.
- **¿Qué pasa si un CGI se cuelga?** Su pipe está en el `epoll` y el resto de clientes siguen atendidos; a los 30 s se mata y se responde 504.
- **¿Y si el cliente se desconecta a mitad?** `recv` devuelve 0 → se cierra el fd, se quita del `epoll` y, si tenía CGI, se mata el proceso.
- **¿Chunked?** Se espera al chunk `0`, se des-chunkea y al CGI le llega el body completo con su `CONTENT_LENGTH`; el CGI ve EOF al cerrar el pipe.
- **¿Archivos de disco?** Se leen/escriben directamente (el subject los excluye del `poll`).
