#include "../cgi-includes/parserCgi.hpp"
#include "../request/parseInputRequest.hpp"
#include "../response/parseResponse.hpp"
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Returns the value of header `name` from rawRequest (case-insensitive),
// or "" if not present. Used to forward HTTP_* environment variables.
// Only the header block is scanned: the request line has no header of its own
// and a line of the body must never be mistaken for one.
static std::string getHeaderValue(const std::string &raw, const std::string &name) {
  std::string lowerName = name;
  for (size_t i = 0; i < lowerName.size(); ++i)
    lowerName[i] = static_cast<char>(tolower(lowerName[i]));

  size_t limit = raw.find("\r\n\r\n");
  if (limit == std::string::npos) limit = raw.size();

  size_t pos = raw.find("\r\n");
  if (pos == std::string::npos) return "";
  pos += 2;

  while (pos < limit) {
    size_t eol = raw.find("\r\n", pos);
    if (eol == std::string::npos || eol > limit) eol = limit;
    std::string line = raw.substr(pos, eol - pos);
    pos = eol + 2;

    size_t c = line.find(':');
    if (c == std::string::npos) continue;
    std::string key = line.substr(0, c);
    for (size_t i = 0; i < key.size(); ++i)
      key[i] = static_cast<char>(tolower(key[i]));
    if (key != lowerName) continue;

    std::string val = line.substr(c + 1);
    while (!val.empty() && (val[0] == ' ' || val[0] == '\t'))
      val.erase(0, 1);
    while (!val.empty() && (val[val.size() - 1] == ' ' ||
                            val[val.size() - 1] == '\t' ||
                            val[val.size() - 1] == '\r'))
      val.erase(val.size() - 1, 1);
    return val;
  }
  return "";
}

// Reenvía TODAS las cabeceras de la petición como HTTP_* (CGI/1.1: guiones a
// guiones bajos y todo en mayúsculas). Con una lista fija se quedaban fuera las
// cabeceras que el cliente inventa, y el tester oficial manda una propia
// (X-Secret-Header-For-Test) que el CGI necesita ver.
// Content-Type y Content-Length se omiten: van sin prefijo y ya se han añadido.
static void addHttpHeaderEnv(const std::string &raw,
                             std::vector<std::string> &env) {
  size_t limit = raw.find("\r\n\r\n");
  if (limit == std::string::npos) limit = raw.size();

  size_t pos = raw.find("\r\n");
  if (pos == std::string::npos) return;
  pos += 2;

  while (pos < limit) {
    size_t eol = raw.find("\r\n", pos);
    if (eol == std::string::npos || eol > limit) eol = limit;
    std::string line = raw.substr(pos, eol - pos);
    pos = eol + 2;

    size_t c = line.find(':');
    if (c == std::string::npos) continue;
    std::string key = line.substr(0, c);
    std::string val = line.substr(c + 1);
    while (!val.empty() && (val[0] == ' ' || val[0] == '\t'))
      val.erase(0, 1);
    while (!val.empty() && (val[val.size() - 1] == ' ' ||
                            val[val.size() - 1] == '\t' ||
                            val[val.size() - 1] == '\r'))
      val.erase(val.size() - 1, 1);

    for (size_t i = 0; i < key.size(); ++i) {
      if (key[i] == '-')
        key[i] = '_';
      else
        key[i] = static_cast<char>(toupper(key[i]));
    }
    if (key.empty() || key == "CONTENT_TYPE" || key == "CONTENT_LENGTH")
      continue;

    env.push_back("HTTP_" + key + "=" + val);
  }
}

static std::string extractBody(const std::string &raw) {
  size_t p = raw.find("\r\n\r\n");
  if (p == std::string::npos) return std::string();
  std::string rawBody = raw.substr(p + 4);
  if (ParseInputRequest::isChunked(raw))
    return ParseInputRequest::decodeChunked(raw);
  return rawBody;
}

static std::string cgiError(int code, const std::string &message) {
  ParseResponse r;
  r.setStatus(code, message);
  r.setHeader("Content-Type", "text/html");
  std::ostringstream body;
  body << "<html><body><h1>" << code << " " << message << "</h1></body></html>";
  r.setBody(body.str());
  return r.build();
}

// Build a null-terminated envp suitable for execve from a vector of
// "KEY=VALUE" strings.
static char **buildEnvp(const std::vector<std::string> &env) {
  char **e = new char *[env.size() + 1];
  for (size_t i = 0; i < env.size(); ++i)
    e[i] = strdup(env[i].c_str());
  e[env.size()] = NULL;
  return e;
}

static void freeEnvp(char **e) {
  for (size_t i = 0; e[i]; ++i) std::free(e[i]);
  delete[] e;
}

// Convert the CGI stdout into a status line + the original headers/body.
// We pass the CGI output through as-is when it looks like a full HTTP
// response, but the standard CGI contract says CGI only writes headers
// followed by a blank line and a body, so we synthesize "HTTP/1.1 200 OK".
static std::string buildHttpResponse(const std::string &cgiOutput) {
  size_t sep = cgiOutput.find("\r\n\r\n");
  if (sep == std::string::npos)
    sep = cgiOutput.find("\n\n");
  std::string headers;
  std::string body;
  if (sep == std::string::npos) {
    body = cgiOutput;
  } else {
    size_t hdrLen = (cgiOutput[sep] == '\r') ? sep + 4 : sep + 2;
    headers = cgiOutput.substr(0, sep);
    body = cgiOutput.substr(hdrLen);
  }

  int status = 200;
  std::string statusMsg = "OK";
  std::string contentType = "text/plain";

  std::istringstream iss(headers);
  std::string line;
  std::vector<std::pair<std::string, std::string> > hdrs;
  while (std::getline(iss, line)) {
    if (!line.empty() && line[line.size() - 1] == '\r')
      line.erase(line.size() - 1, 1);
    if (line.empty()) continue;
    size_t c = line.find(':');
    if (c == std::string::npos) continue;
    std::string key = line.substr(0, c);
    std::string val = line.substr(c + 1);
    while (!val.empty() && (val[0] == ' ' || val[0] == '\t'))
      val.erase(0, 1);

    std::string lowerKey = key;
    for (size_t i = 0; i < lowerKey.size(); ++i)
      lowerKey[i] = static_cast<char>(tolower(lowerKey[i]));

    if (lowerKey == "status") {
      std::istringstream ss(val);
      ss >> status;
      size_t sp = val.find(' ');
      if (sp != std::string::npos) statusMsg = val.substr(sp + 1);
    } else if (lowerKey == "content-type") {
      contentType = val;
    } else {
      hdrs.push_back(std::make_pair(key, val));
    }
  }

  std::ostringstream resp;
  resp << "HTTP/1.1 " << status << " " << statusMsg << "\r\n";
  resp << "Content-Type: " << contentType << "\r\n";
  for (size_t i = 0; i < hdrs.size(); ++i)
    resp << hdrs[i].first << ": " << hdrs[i].second << "\r\n";
  resp << "Content-Length: " << body.size() << "\r\n";
  resp << "Connection: close\r\n\r\n";
  resp << body;
  return resp.str();
}

std::string Parsercgi::execute(const std::string &interpreter,
                              const std::string &scriptPath,
                              const std::string &rawRequest,
                              const std::string &queryString,
                              const std::string &serverHost,
                              int serverPort,
                              const std::vector<int> &fdsToClose) {
  // Método y URL salen de la request line, no de las cabeceras.
  std::string reqMethod;
  std::string urlPath;
  size_t rl = rawRequest.find("\r\n");
  if (rl != std::string::npos) {
    std::string line = rawRequest.substr(0, rl);
    size_t s1 = line.find(' ');
    size_t s2 = (s1 == std::string::npos) ? std::string::npos
                                          : line.find(' ', s1 + 1);
    if (s1 != std::string::npos)
      reqMethod = line.substr(0, s1);
    if (s1 != std::string::npos && s2 != std::string::npos)
      urlPath = line.substr(s1 + 1, s2 - s1 - 1);
    else if (s1 != std::string::npos)
      urlPath = line.substr(s1 + 1);
  }
  if (reqMethod.empty())
    reqMethod = "POST";

  // SCRIPT_NAME es la ruta URL sin query; REQUEST_URI conserva la original.
  std::string scriptUrl = urlPath;
  size_t qm = scriptUrl.find('?');
  if (qm != std::string::npos)
    scriptUrl = scriptUrl.substr(0, qm);

  // El body se des-chunkea aquí, antes del fork, porque el hijo necesita su
  // tamaño real para CONTENT_LENGTH: una petición chunked no trae cabecera
  // Content-Length, y un CGI conforme a CGI/1.1 leería 0 bytes de stdin.
  std::string body = extractBody(rawRequest);
  std::ostringstream clSS;
  clSS << body.size();
  std::string contentLength = clSS.str();

  int inPipe[2];
  int outPipe[2];
  if (pipe(inPipe) == -1 || pipe(outPipe) == -1)
    return cgiError(500, "Internal Server Error");
  int inRead = inPipe[0];
  int inWrite = inPipe[1];
  int outRead = outPipe[0];
  int outWrite = outPipe[1];

  pid_t pid = fork();
  if (pid == -1) {
    close(inRead);
    close(inWrite);
    close(outRead);
    close(outWrite);
    return cgiError(500, "Internal Server Error");
  }

  if (pid == 0) {
    // Child.
    close(inWrite);
    close(outRead);
    dup2(inRead, 0);
    dup2(outWrite, 1);
    close(inRead);
    close(outWrite);

    // Los fds del servidor se heredan en el fork. Hay que cerrarlos a mano:
    // el subject solo permite fcntl(fd, F_SETFL, O_NONBLOCK), así que no
    // podemos marcarlos FD_CLOEXEC.
    for (size_t i = 0; i < fdsToClose.size(); ++i) {
      if (fdsToClose[i] > 2)
        close(fdsToClose[i]);
    }

    std::string contentType = getHeaderValue(rawRequest, "Content-Type");
    std::string hostHeader = getHeaderValue(rawRequest, "Host");
    std::string serverProtocol = "HTTP/1.1";

    std::string queryEnv = queryString;

    std::string scriptName = scriptUrl;

    std::ostringstream portSS;
  portSS << serverPort;
  std::string portStr = portSS.str();

    std::vector<std::string> env;
    env.push_back("REQUEST_METHOD=" + reqMethod);
    env.push_back("CONTENT_TYPE=" + contentType);
    env.push_back("CONTENT_LENGTH=" + contentLength);
    env.push_back("QUERY_STRING=" + queryEnv);
    env.push_back("SCRIPT_NAME=" + scriptName);
    env.push_back("SCRIPT_FILENAME=" + scriptPath);
    // PATH_INFO va sin query string: es una ruta, y el CGI la contrasta con el
    // script que se le pasa. El query solo viaja en QUERY_STRING y REQUEST_URI.
    env.push_back("PATH_INFO=" + scriptUrl);
    env.push_back("PATH_TRANSLATED=" + scriptPath);
    env.push_back("REQUEST_URI=" + urlPath);
    env.push_back("SERVER_PROTOCOL=" + serverProtocol);
    env.push_back("SERVER_NAME=" + hostHeader);
    env.push_back("SERVER_PORT=" + portStr);
    env.push_back("REMOTE_ADDR=" + std::string(serverHost));
    env.push_back("REMOTE_PORT=0");
    env.push_back("GATEWAY_INTERFACE=CGI/1.1");
    env.push_back("REDIRECT_STATUS=200");

    // HTTP_HOST y el resto de cabeceras (incluida Host) las añade esta llamada.
    addHttpHeaderEnv(rawRequest, env);

    char **envp = buildEnvp(env);

    // El CGI debe ejecutarse en el directorio del script para que sus accesos
    // por ruta relativa funcionen; tras el chdir el script se referencia como
    // "./<fichero>".
    std::string scriptFile = scriptPath;
    size_t slash = scriptPath.rfind('/');
    if (slash != std::string::npos) {
      std::string scriptDir = scriptPath.substr(0, slash);
      scriptFile = scriptPath.substr(slash + 1);
      if (!scriptDir.empty())
        chdir(scriptDir.c_str());
    }

    std::vector<std::string> args;
    if (!interpreter.empty()) {
      args.push_back(interpreter);
    }
    args.push_back("./" + scriptFile);
    char **argv = new char *[args.size() + 1];
    for (size_t i = 0; i < args.size(); ++i)
      argv[i] = strdup(args[i].c_str());
    argv[args.size()] = NULL;

    execve(argv[0], argv, envp);

    // If execve returns, it failed.
    freeEnvp(envp);
    for (size_t i = 0; argv[i]; ++i) std::free(argv[i]);
    delete[] argv;
    _exit(127);
  }

  // Parent.
  close(inRead);
  close(outWrite);

  // Los dos pipes se atienden a la vez con un solo poll(). Escribir el body
  // entero antes de leer la salida provoca un abrazo mortal: el CGI llena su
  // pipe de stdout (64 KB) y se bloquea, deja de leer stdin, el pipe de
  // entrada se llena y el servidor se bloquea también.
  fcntl(inWrite, F_SETFL, O_NONBLOCK);
  fcntl(outRead, F_SETFL, O_NONBLOCK);

  // Un body vacío se señaliza cerrando stdin del CGI de inmediato: el EOF es
  // lo que hace terminar a los CGI que leen hasta el final.
  if (body.empty()) {
    close(inWrite);
    inWrite = -1;
  }

  std::string out;
  size_t written = 0;
  bool outEof = false;
  bool timedOut = false;
  time_t deadline = time(NULL) + CGI_TIMEOUT_SEC;

  while (!outEof) {
    struct pollfd pfd[2];
    int nfds = 0;
    int inIdx = -1;

    if (inWrite != -1) {
      pfd[nfds].fd = inWrite;
      pfd[nfds].events = POLLOUT;
      pfd[nfds].revents = 0;
      inIdx = nfds;
      ++nfds;
    }
    int outIdx = nfds;
    pfd[nfds].fd = outRead;
    pfd[nfds].events = POLLIN;
    pfd[nfds].revents = 0;
    ++nfds;

    int ready = poll(pfd, nfds, 100);
    if (ready == -1)
      break;

    if (inIdx != -1 && pfd[inIdx].revents) {
      if (pfd[inIdx].revents & POLLOUT) {
        ssize_t n = write(inWrite, body.data() + written, body.size() - written);
        if (n > 0) {
          written += static_cast<size_t>(n);
          if (written >= body.size()) {
            close(inWrite);
            inWrite = -1;
          }
        } else {
          // El CGI cerró stdin sin consumir todo el body: no es un error,
          // se deja de escribir y se sigue drenando su salida.
          close(inWrite);
          inWrite = -1;
        }
      } else if (pfd[inIdx].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        close(inWrite);
        inWrite = -1;
      }
    }

    if (pfd[outIdx].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) {
      char buf[BUFFER_SIZE];
      ssize_t n = read(outRead, buf, sizeof(buf));
      if (n > 0)
        out.append(buf, n);
      else
        outEof = true;
    }

    if (!outEof && time(NULL) > deadline) {
      timedOut = true;
      break;
    }
  }

  if (inWrite != -1)
    close(inWrite);
  close(outRead);

  int status = 0;
  if (timedOut) {
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return cgiError(504, "Gateway Timeout");
  }

  // stdout del CGI está en EOF, así que normalmente ya ha terminado, pero un
  // proceso puede cerrar stdout y seguir vivo: se espera sin bloquear y se
  // mata si se pasa del margen.
  bool reaped = false;
  time_t waitUntil = time(NULL) + 2;
  while (time(NULL) <= waitUntil) {
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid || r == -1) {
      reaped = true;
      break;
    }
    poll(NULL, 0, 1);
  }
  if (!reaped) {
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return cgiError(504, "Gateway Timeout");
  }

  // 127 es el código con el que sale el hijo cuando execve falla (intérprete
  // inexistente o sin permiso de ejecución).
  if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
    return cgiError(502, "Bad Gateway");

  return buildHttpResponse(out);
}