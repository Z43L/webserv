#include "../cgi-includes/parserCgi.hpp"
#include "../request/parseInputRequest.hpp"
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sstream>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Returns the value of header `name` from rawRequest (case-insensitive),
// or "" if not present. Used to forward HTTP_* environment variables.
static std::string getHeaderValue(const std::string &raw, const std::string &name) {
  std::string lowerName = name;
  for (size_t i = 0; i < lowerName.size(); ++i)
    lowerName[i] = static_cast<char>(tolower(lowerName[i]));

  size_t pos = 0;
  while (pos < raw.size()) {
    size_t eol = raw.find("\r\n", pos);
    if (eol == std::string::npos) eol = raw.size();
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

static std::string extractBody(const std::string &raw) {
  size_t p = raw.find("\r\n\r\n");
  if (p == std::string::npos) return std::string();
  std::string rawBody = raw.substr(p + 4);
  if (ParseInputRequest::isChunked(raw))
    return ParseInputRequest::decodeChunked(raw);
  return rawBody;
}

static std::string readAll(int fd) {
  char buf[BUFFER_SIZE];
  std::string out;
  while (true) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n > 0)
      out.append(buf, n);
    else
      break;
  }
  return out;
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
                              int serverPort) {
  std::string urlPath;
  size_t rl = rawRequest.find("\r\n");
  if (rl != std::string::npos) {
    std::string line = rawRequest.substr(0, rl);
    size_t s1 = line.find(' ');
    size_t s2 = (s1 == std::string::npos) ? std::string::npos
                                          : line.find(' ', s1 + 1);
    if (s1 != std::string::npos && s2 != std::string::npos)
      urlPath = line.substr(s1 + 1, s2 - s1 - 1);
    else if (s1 != std::string::npos)
      urlPath = line.substr(s1 + 1);
  }

  int inPipe[2];
  int outPipe[2];
  if (pipe(inPipe) == -1 || pipe(outPipe) == -1)
    return std::string();
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
    return std::string();
  }

  if (pid == 0) {
    // Child.
    close(inWrite);
    close(outRead);
    dup2(inRead, 0);
    dup2(outWrite, 1);
    close(inRead);
    close(outWrite);

    std::string reqMethod = getHeaderValue(rawRequest, "REQUEST_METHOD");
    if (reqMethod.empty()) reqMethod = "POST";
    std::string contentType = getHeaderValue(rawRequest, "Content-Type");
    std::string contentLength = getHeaderValue(rawRequest, "Content-Length");
    std::string hostHeader = getHeaderValue(rawRequest, "Host");
    std::string serverProtocol = "HTTP/1.1";

    std::string queryEnv = queryString;

    // Strip query from script path for SCRIPT_NAME; full URL goes to
    // REQUEST_URI. PATH_INFO is the portion of the URL after SCRIPT_NAME
    // (typically empty for our routes); we set it to "/" so CGI handlers
    // that check getenv("PATH_INFO") still see a valid value.
    std::string scriptName = scriptPath;
    std::string pathInfo = "/";

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
    env.push_back("PATH_INFO=" + urlPath);
    env.push_back("PATH_TRANSLATED=" + scriptPath);
    env.push_back("REQUEST_URI=" + urlPath);
    env.push_back("SERVER_PROTOCOL=" + serverProtocol);
    env.push_back("SERVER_NAME=" + hostHeader);
    env.push_back("SERVER_PORT=" + portStr);
    env.push_back("HTTP_HOST=" + hostHeader);
    env.push_back("REMOTE_ADDR=" + std::string(serverHost));
    env.push_back("REMOTE_PORT=0");
    env.push_back("GATEWAY_INTERFACE=CGI/1.1");
    env.push_back("REDIRECT_STATUS=200");

    // Forward common request headers as HTTP_* env vars.
    static const char *forwarded[] = {
        "User-Agent", "Accept", "Accept-Language", "Accept-Encoding",
        "Referer", "Cookie", NULL};
    for (int i = 0; forwarded[i]; ++i) {
      std::string v = getHeaderValue(rawRequest, forwarded[i]);
      if (!v.empty()) {
        std::string key = std::string("HTTP_") + forwarded[i];
        for (size_t j = 0; j < key.size(); ++j) {
          if (key[j] == '-')
            key[j] = '_';
          else
            key[j] = static_cast<char>(toupper(key[j]));
        }
        env.push_back(key + "=" + v);
      }
    }

    char **envp = buildEnvp(env);

    std::vector<std::string> args;
    if (!interpreter.empty()) {
      args.push_back(interpreter);
    }
    args.push_back(scriptPath);
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

  std::string body = extractBody(rawRequest);
  ssize_t written = 0;
  while (written < static_cast<ssize_t>(body.size())) {
    ssize_t n = write(inWrite, body.data() + written, body.size() - written);
    if (n <= 0) break;
    written += n;
  }
  close(inWrite);

  std::string out = readAll(outRead);
  close(outRead);

  int status = 0;
  waitpid(pid, &status, 0);

  return buildHttpResponse(out);
}