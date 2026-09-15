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

  std::string scriptUrl = urlPath;
  size_t qm = scriptUrl.find('?');
  if (qm != std::string::npos)
    scriptUrl = scriptUrl.substr(0, qm);

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
    close(inWrite);
    close(outRead);
    dup2(inRead, 0);
    dup2(outWrite, 1);
    close(inRead);
    close(outWrite);

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

    {
      size_t hdrLimit = rawRequest.find("\r\n\r\n");
      if (hdrLimit == std::string::npos) hdrLimit = rawRequest.size();
      size_t pos = rawRequest.find("\r\n");
      if (pos != std::string::npos) {
        pos += 2;
        while (pos < hdrLimit) {
          size_t eol = rawRequest.find("\r\n", pos);
          if (eol == std::string::npos || eol > hdrLimit) eol = hdrLimit;
          std::string line = rawRequest.substr(pos, eol - pos);
          pos = eol + 2;

          size_t c = line.find(':');
          if (c == std::string::npos) continue;
          std::string key = line.substr(0, c);
          std::string val = line.substr(c + 1);
          while (!val.empty() && (val[0] == ' ' || val[0] == '\t'))
            val.erase(0, 1);
          while (!val.empty() &&
                 (val[val.size() - 1] == ' ' || val[val.size() - 1] == '\t' ||
                  val[val.size() - 1] == '\r'))
            val.erase(val.size() - 1, 1);

          std::string envKey = "HTTP_";
          for (size_t j = 0; j < key.size(); ++j) {
            char ch = key[j];
            if (ch == '-')
              envKey += '_';
            else
              envKey += static_cast<char>(toupper(static_cast<unsigned char>(ch)));
          }
          env.push_back(envKey + "=" + val);
        }
      }
    }

    char **envp = buildEnvp(env);

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

    freeEnvp(envp);
    for (size_t i = 0; argv[i]; ++i) std::free(argv[i]);
    delete[] argv;
    _exit(127);
  }

  close(inRead);
  close(outWrite);

  fcntl(inWrite, F_SETFL, O_NONBLOCK);
  fcntl(outRead, F_SETFL, O_NONBLOCK);

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

  if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
    return cgiError(502, "Bad Gateway");

  return buildHttpResponse(out);
}
