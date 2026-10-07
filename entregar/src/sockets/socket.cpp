#include "../sockets-includes/socket.hpp"
#include "../request/parseInputRequest.hpp"
#include "../response/parseResponse.hpp"
#include "../cgi-includes/parserCgi.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int Socket::_epollFd = -1;
std::map<int, Socket *> Socket::_fdOwner;
std::vector<std::pair<pid_t, time_t> > Socket::_pendingChildren;

Socket::Socket()
    : _fd(-1), _port(0), _ip(""), _state(SOCKET_LISTENING),
      _isNonBlocking(false), _docRoot("./web"), _indexFile("index.html"),
      _maxBodySize(0), _readTimeoutSec(0) {}

Socket::~Socket() { this->closeSocket(); }

bool Socket::setNonBlocking(int fd) {
  if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
    return false;
  }
  return true;
}

int Socket::bindAndListen(const std::string &ip, int port, int backlog) {
  this->_port = port;
  this->_ip = ip;

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    std::cout << "error when open socket" << std::endl;
    return -1;
  }

  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  if (!setNonBlocking(fd)) {
    close(fd);
    std::cout << "error when saving socket as non-blocking" << std::endl;
    return -1;
  }

  struct sockaddr_in address;
  std::memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  if (ip.empty() || ip == "0.0.0.0") {
    address.sin_addr.s_addr = INADDR_ANY;
  } else if (ip == "localhost") {
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  } else if (inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1) {
    std::cerr << "Invalid host address: " << ip << std::endl;
    close(fd);
    return -1;
  }
  address.sin_port = htons(port);
  this->addr = address;

  if (bind(fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
    std::cerr << "Error en el bind" << std::endl;
    close(fd);
    return -1;
  }

  if (listen(fd, backlog) == -1) {
    std::cerr << "Error en el listen" << std::endl;
    close(fd);
    return -1;
  }

  this->_fd = fd;
  this->_state = SOCKET_LISTENING;
  return fd;
}

void Socket::setDocRoot(const std::string &docRoot) { _docRoot = docRoot; }
void Socket::setIndexFile(const std::string &indexFile) {
  _indexFile = indexFile;
}
void Socket::setLocations(const std::vector<LocationBlock> &locations) {
  _locations = locations;
}
void Socket::setErrorPages(const std::vector<ErrorPage> &errorPages) {
  _errorPages = errorPages;
}
void Socket::setMaxBodySize(long size) { _maxBodySize = size; }
void Socket::setReadTimeout(long seconds) { _readTimeoutSec = seconds; }

static bool endsWith(const std::string &s, const std::string &suffix) {
  if (suffix.size() > s.size())
    return false;
  return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string htmlEscape(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    switch (s[i]) {
    case '&': out += "&amp;"; break;
    case '<': out += "&lt;"; break;
    case '>': out += "&gt;"; break;
    case '"': out += "&#34;"; break;
    default: out += s[i];
    }
  }
  return out;
}

static bool isDirectory(const std::string &path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0)
    return false;
  return S_ISDIR(st.st_mode);
}

static std::string generateAutoindex(const std::string &dirPath,
                                     const std::string &urlPath) {
  DIR *dir = opendir(dirPath.c_str());
  if (!dir) {
    return std::string();
  }
  std::ostringstream body;
  body << "<html><head><title>Index of " << htmlEscape(urlPath)
       << "</title></head><body>";
  body << "<h1>Index of " << htmlEscape(urlPath) << "</h1><hr><ul>";
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    std::string name = ent->d_name;
    if (name == "." || name == "..")
      continue;
    std::string link = urlPath;
    if (link.empty() || link[link.size() - 1] != '/')
      link += '/';
    link += name;
    body << "<li><a href=\"" << htmlEscape(link) << "\">"
         << htmlEscape(name) << "</a></li>";
  }
  closedir(dir);
  body << "</ul><hr></body></html>";
  return body.str();
}

static bool methodAllowed(const std::vector<std::string> &methods,
                         const std::string &m) {
  for (size_t i = 0; i < methods.size(); ++i)
    if (methods[i] == m)
      return true;
  return false;
}

static LocationBlock matchLocation(const std::vector<LocationBlock> &locs,
                                   const std::string &url) {
  LocationBlock best;
  size_t bestLen = 0;
  for (size_t i = 0; i < locs.size(); ++i) {
    const std::string &p = locs[i].path;
    if (p.size() <= url.size() &&
        url.compare(0, p.size(), p) == 0 &&
        (p.empty() || p == "/" || url.size() == p.size() ||
         url[p.size()] == '/' || url[p.size()] == '?')) {
      if (p.size() > bestLen) {
        best = locs[i];
        bestLen = p.size();
      }
    }
  }
  return best;
}

static std::string resolvePath(const std::string &url,
                               const LocationBlock &loc,
                               const std::string &serverRoot) {
  std::string rest = url.substr(loc.path.size());
  if (!loc.alias.empty()) {
    std::string p = loc.alias;
    if (!rest.empty() && rest[0] != '/')
      p += '/';
    p += rest;
    return p;
  }
  if (!loc.root.empty()) {
    return loc.root + rest;
  }
  return serverRoot + rest;
}

static std::string extractRequestUrl(const std::string &raw) {
  size_t eol = raw.find("\r\n");
  if (eol == std::string::npos) eol = raw.size();
  size_t first = raw.find(' ');
  if (first == std::string::npos || first >= eol) return "";
  size_t second = raw.find(' ', first + 1);
  size_t end = (second == std::string::npos || second > eol) ? eol : second;
  std::string url = raw.substr(first + 1, end - first - 1);
  size_t q = url.find('?');
  if (q != std::string::npos) url.resize(q);
  return url;
}

static const char *reasonPhrase(int code) {
  switch (code) {
  case 200: return "OK";
  case 201: return "Created";
  case 204: return "No Content";
  case 301: return "Moved Permanently";
  case 302: return "Found";
  case 303: return "See Other";
  case 307: return "Temporary Redirect";
  case 308: return "Permanent Redirect";
  case 400: return "Bad Request";
  case 403: return "Forbidden";
  case 404: return "Not Found";
  case 405: return "Method Not Allowed";
  case 408: return "Request Timeout";
  case 413: return "Payload Too Large";
  case 431: return "Request Header Fields Too Large";
  case 500: return "Internal Server Error";
  case 501: return "Not Implemented";
  case 502: return "Bad Gateway";
  case 504: return "Gateway Timeout";
  case 505: return "HTTP Version Not Supported";
  }
  return "Unknown";
}

static std::string takeQuery(std::string &url) {
  std::string query;
  size_t q = url.find('?');
  if (q != std::string::npos) {
    query = url.substr(q + 1);
    url = url.substr(0, q);
  }
  return query;
}

static std::string percentDecode(const std::string &s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(s[i + 1]) &&
        std::isxdigit(s[i + 2])) {
      out += static_cast<char>(
          std::strtol(s.substr(i + 1, 2).c_str(), NULL, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

static std::string joinMethods(const std::vector<std::string> &methods) {
  std::string allow;
  for (size_t i = 0; i < methods.size(); ++i) {
    if (i)
      allow += ", ";
    allow += methods[i];
  }
  return allow;
}

static size_t requestBodyLength(const std::string &raw) {
  if (ParseInputRequest::isChunked(raw))
    return ParseInputRequest::decodedChunkedSize(raw);
  size_t bodyStart = raw.find("\r\n\r\n");
  return (bodyStart == std::string::npos) ? 0 : raw.size() - bodyStart - 4;
}

static std::string requestBody(const std::string &raw) {
  if (ParseInputRequest::isChunked(raw))
    return ParseInputRequest::decodeChunked(raw);
  size_t bodyStart = raw.find("\r\n\r\n");
  return (bodyStart == std::string::npos) ? "" : raw.substr(bodyStart + 4);
}

static int findCgiExtIndex(const LocationBlock &loc, const std::string &url) {
  for (size_t i = 0; i < loc.cgi_ext.size(); ++i)
    if (endsWith(url, loc.cgi_ext[i]))
      return static_cast<int>(i);
  return -1;
}

static std::string serveHead(const std::string &filePath) {
  ParseResponse r;
  r.setStatus(200, "OK");
  r.setHeader("Content-Type", ParseResponse::getMimeType(filePath));
  struct stat st;
  if (stat(filePath.c_str(), &st) == 0) {
    std::ostringstream ss;
    ss << static_cast<long>(st.st_size);
    r.setHeader("Content-Length", ss.str());
  }
  r.setBody("");
  return r.build();
}

std::string Socket::errorResponse(int code, const std::string &headerKey,
                                  const std::string &headerValue) const {
  ParseResponse r;
  r.setStatus(code, reasonPhrase(code));
  if (!headerKey.empty())
    r.setHeader(headerKey, headerValue);
  r.setHeader("Content-Type", "text/html");
  for (size_t i = 0; i < _errorPages.size(); ++i) {
    if (_errorPages[i].code != code)
      continue;
    std::string p = _docRoot + _errorPages[i].path;
    if (ParseResponse::fileExists(p) && !isDirectory(p)) {
      r.setBodyFromFile(p);
      r.setStatus(code, reasonPhrase(code));
      return r.build();
    }
  }
  std::ostringstream body;
  body << "<!DOCTYPE html><html><head><title>" << code << " "
       << reasonPhrase(code) << "</title></head><body><h1>" << code << " "
       << reasonPhrase(code) << "</h1><hr><p>webserv</p></body></html>";
  r.setBody(body.str());
  return r.build();
}

long Socket::effectiveMaxFor(const std::string &rawRequest) const {
  std::string url = extractRequestUrl(rawRequest);
  LocationBlock loc = matchLocation(_locations, url);
  if (loc.client_max_body_size >= 0)
    return loc.client_max_body_size;
  return _maxBodySize;
}

std::string Socket::checkBodyLimits(const std::string &rawRequest,
                                    long declaredLen) const {
  long effectiveMax = effectiveMaxFor(rawRequest);
  if (effectiveMax <= 0)
    return "";
  if (declaredLen > effectiveMax)
    return errorResponse(413);
  if (static_cast<long>(requestBodyLength(rawRequest)) > effectiveMax)
    return errorResponse(413);
  return "";
}

std::string Socket::runCgi(int clientFd, const std::string &url,
                           const LocationBlock &loc,
                           const std::string &rawRequest,
                           const std::string &queryString, size_t extIdx) {
  std::string filePath = resolvePath(url, loc, _docRoot);
  if (filePath.find("..") != std::string::npos)
    return errorResponse(403);
  if (rawRequest.compare(0, 4, "GET ") == 0 &&
      (!ParseResponse::fileExists(filePath) || isDirectory(filePath)))
    return errorResponse(404);

  std::string interpreter;
  if (extIdx < loc.cgi_path.size())
    interpreter = loc.cgi_path[extIdx];
  else if (!loc.cgi_path.empty())
    interpreter = loc.cgi_path[0];

  std::vector<int> fdsToClose;
  for (std::map<int, Socket *>::const_iterator it = _fdOwner.begin();
       it != _fdOwner.end(); ++it)
    fdsToClose.push_back(it->first);
  if (_epollFd != -1)
    fdsToClose.push_back(_epollFd);

  ClientSession &session = active_clients[clientFd];
  session.cgi_body = requestBody(rawRequest);

  CgiProcess cgi;
  if (!Parsercgi::start(interpreter, filePath, rawRequest, queryString,
                        session.cgi_body.size(), _ip, _port, fdsToClose,
                        cgi)) {
    std::string().swap(session.cgi_body);
    return errorResponse(500);
  }

  session.cgi_pid = cgi.pid;
  session.cgi_in = cgi.inFd;
  session.cgi_out = cgi.outFd;
  session.cgi_written = 0;
  session.cgi_output.clear();
  session.cgi_start = std::time(NULL);

  _cgiPipes[cgi.outFd] = clientFd;
  watchFd(cgi.outFd, this, EPOLLIN);
  if (session.cgi_body.empty()) {
    close(cgi.inFd);
    session.cgi_in = -1;
  } else {
    _cgiPipes[cgi.inFd] = clientFd;
    watchFd(cgi.inFd, this, EPOLLOUT);
  }
  changeFd(clientFd, 0);
  return "";
}

static std::string safeFileName(const std::string &name) {
  std::string base = name;
  size_t slash = base.find_last_of("/\\");
  if (slash != std::string::npos)
    base = base.substr(slash + 1);
  if (base.empty() || base == "." || base == "..")
    return "";
  return base;
}

static std::string generatedFileName() {
  static unsigned long counter = 0;
  std::ostringstream ss;
  ss << "upload_" << std::time(NULL) << "_" << counter++;
  return ss.str();
}

static bool writeFile(const std::string &path, const std::string &data,
                      size_t start, size_t len) {
  std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
  if (!f.is_open())
    return false;
  f.write(data.data() + start, static_cast<std::streamsize>(len));
  return f.good();
}

std::string Socket::handleUpload(const std::string &url,
                                 const LocationBlock &loc,
                                 const std::string &rawRequest) {
  std::string dir = loc.upload_store;
  if (!isDirectory(dir))
    return errorResponse(500);
  if (dir[dir.size() - 1] != '/')
    dir += '/';

  ParseInputRequest parser;
  parser.parse(rawRequest.substr(0, rawRequest.find("\r\n\r\n") + 4));
  std::string contentType = parser.getType();
  std::string body = requestBody(rawRequest);
  std::vector<std::string> saved;

  size_t bpos = contentType.find("boundary=");
  if (contentType.compare(0, 19, "multipart/form-data") == 0 &&
      bpos != std::string::npos) {
    std::string boundary = contentType.substr(bpos + 9);
    size_t semi = boundary.find(';');
    if (semi != std::string::npos)
      boundary.resize(semi);
    if (boundary.size() >= 2 && boundary[0] == '"')
      boundary = boundary.substr(1, boundary.size() - 2);
    std::string delim = "--" + boundary;

    size_t pos = body.find(delim);
    while (pos != std::string::npos) {
      pos += delim.size();
      if (body.compare(pos, 2, "--") == 0)
        break;
      size_t hdrEnd = body.find("\r\n\r\n", pos);
      if (hdrEnd == std::string::npos)
        break;
      std::string partHeaders = body.substr(pos, hdrEnd - pos);
      size_t dataStart = hdrEnd + 4;
      size_t next = body.find("\r\n" + delim, dataStart);
      if (next == std::string::npos)
        break;

      size_t fn = partHeaders.find("filename=\"");
      if (fn != std::string::npos) {
        size_t fnEnd = partHeaders.find('"', fn + 10);
        std::string name = safeFileName(partHeaders.substr(fn + 10, fnEnd - fn - 10));
        if (!name.empty()) {
          if (!writeFile(dir + name, body, dataStart, next - dataStart))
            return errorResponse(500);
          saved.push_back(name);
        }
      }
      pos = next + 2;
    }
    if (saved.empty())
      return errorResponse(400);
  } else {
    std::string name = safeFileName(url.substr(loc.path.size()));
    if (name.empty())
      name = generatedFileName();
    if (!writeFile(dir + name, body, 0, body.size()))
      return errorResponse(500);
    saved.push_back(name);
  }

  std::ostringstream html;
  html << "<!DOCTYPE html><html><body><h1>201 Created</h1><ul>";
  for (size_t i = 0; i < saved.size(); ++i)
    html << "<li>" << htmlEscape(saved[i]) << "</li>";
  html << "</ul></body></html>";
  ParseResponse r;
  r.setStatus(201, "Created");
  r.setHeader("Content-Type", "text/html");
  r.setBody(html.str());
  return r.build();
}

std::string Socket::handleDelete(const std::string &filePath) {
  if (!ParseResponse::fileExists(filePath))
    return errorResponse(404);
  if (isDirectory(filePath))
    return errorResponse(403);
  if (std::remove(filePath.c_str()) != 0)
    return errorResponse(403);
  ParseResponse r;
  r.setStatus(204, "No Content");
  r.setBody("");
  return r.build();
}

bool Socket::resolveDirectory(std::string &filePath, const std::string &url,
                              const LocationBlock &loc, std::string &out) {
  std::string indexFile = !loc.index.empty() ? loc.index : _indexFile;
  std::string withIndex = filePath;
  if (!withIndex.empty() && withIndex[withIndex.size() - 1] != '/')
    withIndex += '/';
  withIndex += indexFile;

  if (ParseResponse::fileExists(withIndex)) {
    filePath = withIndex;
    return false;
  }

  if (loc.autoindex && (url == loc.path || url + "/" == loc.path ||
                        url == loc.path + "/")) {
    std::string urlPath = url;
    if (urlPath.empty() || urlPath[urlPath.size() - 1] != '/')
      urlPath += '/';
    ParseResponse r;
    r.setStatus(200, "OK");
    r.setHeader("Content-Type", "text/html");
    r.setBody(generateAutoindex(filePath, urlPath));
    out = r.build();
    return true;
  }

  out = errorResponse(404);
  return true;
}

std::string Socket::routeRequest(int clientFd, const std::string &rawRequest) {
  size_t headerEnd = rawRequest.find("\r\n\r\n");
  ParseInputRequest parser;
  parser.parse(rawRequest.substr(0, headerEnd + 4));

  std::string method = parser.getMethod();
  std::string url = parser.getUrl();

  std::string requestLine = rawRequest.substr(0, rawRequest.find("\r\n"));
  size_t lastSpace = requestLine.rfind(' ');
  std::string version = (lastSpace == std::string::npos)
                            ? ""
                            : requestLine.substr(lastSpace + 1);
  if (method.empty() || url.empty() || url[0] != '/' ||
      std::count(requestLine.begin(), requestLine.end(), ' ') != 2)
    return errorResponse(400);
  if (version != "HTTP/1.1" && version != "HTTP/1.0")
    return errorResponse(505);

  std::string queryString = takeQuery(url);
  url = percentDecode(url);

  if (method != "GET" && method != "POST" && method != "DELETE" &&
      method != "HEAD")
    return errorResponse(501);

  LocationBlock loc = matchLocation(_locations, url);

  std::string tooLarge = checkBodyLimits(rawRequest, parser.getContentLength());
  if (!tooLarge.empty())
    return tooLarge;

  std::vector<std::string> methods = loc.allow_methods;
  if (methods.empty())
    methods.push_back("GET");
  if (!methodAllowed(methods, method))
    return errorResponse(405, "Allow", joinMethods(methods));

  if (!loc.return_path.empty()) {
    ParseResponse r;
    r.setStatus(loc.return_code, reasonPhrase(loc.return_code));
    r.setHeader("Location", loc.return_path);
    r.setBody("");
    return r.build();
  }

  if ((method == "GET" || method == "POST") && !loc.cgi_ext.empty()) {
    int extIdx = findCgiExtIndex(loc, url);
    if (extIdx != -1)
      return runCgi(clientFd, url, loc, rawRequest, queryString,
                    static_cast<size_t>(extIdx));
  }

  if (method == "POST" && !loc.upload_store.empty())
    return handleUpload(url, loc, rawRequest);

  std::string filePath = resolvePath(url, loc, _docRoot);
  if (filePath.find("..") != std::string::npos)
    return errorResponse(403);

  if (method == "DELETE")
    return handleDelete(filePath);

  if (isDirectory(filePath)) {
    std::string response;
    if (resolveDirectory(filePath, url, loc, response))
      return response;
  }

  if (!ParseResponse::fileExists(filePath))
    return errorResponse(404);
  if (access(filePath.c_str(), R_OK) != 0)
    return errorResponse(403);

  if (method == "HEAD")
    return serveHead(filePath);

  ParseResponse r;
  r.setBodyFromFile(filePath);
  return r.build();
}

bool Socket::watchFd(int fd, Socket *owner, uint32_t events) {
  struct epoll_event ev;
  std::memset(&ev, 0, sizeof(ev));
  ev.events = events;
  ev.data.fd = fd;
  if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, fd, &ev) == -1)
    return false;
  _fdOwner[fd] = owner;
  return true;
}

void Socket::changeFd(int fd, uint32_t events) {
  struct epoll_event ev;
  std::memset(&ev, 0, sizeof(ev));
  ev.events = events;
  ev.data.fd = fd;
  epoll_ctl(_epollFd, EPOLL_CTL_MOD, fd, &ev);
}

void Socket::unwatchFd(int fd) {
  epoll_ctl(_epollFd, EPOLL_CTL_DEL, fd, NULL);
  _fdOwner.erase(fd);
}

void Socket::queueResponse(int fd, const std::string &response) {
  ClientSession &session = active_clients[fd];
  session.write_buffer = response;
  session.write_offset = 0;
  session.is_response_ready = true;
  session.last_activity = std::time(NULL);
  changeFd(fd, EPOLLOUT);
}

void Socket::stopCgi(ClientSession &session) {
  int *pipes[2] = {&session.cgi_in, &session.cgi_out};
  for (int i = 0; i < 2; ++i) {
    if (*pipes[i] == -1)
      continue;
    unwatchFd(*pipes[i]);
    _cgiPipes.erase(*pipes[i]);
    close(*pipes[i]);
    *pipes[i] = -1;
  }
  std::string().swap(session.cgi_body);
}

void Socket::closeClient(int fd) {
  std::map<int, ClientSession>::iterator it = active_clients.find(fd);
  if (it != active_clients.end()) {
    if (it->second.cgi_pid != -1) {
      stopCgi(it->second);
      kill(it->second.cgi_pid, SIGKILL);
      _pendingChildren.push_back(
          std::make_pair(it->second.cgi_pid, std::time(NULL)));
    }
    active_clients.erase(it);
  }
  unwatchFd(fd);
  close(fd);
}

void Socket::finishCgi(int clientFd, bool timedOut) {
  ClientSession &session = active_clients[clientFd];
  pid_t pid = session.cgi_pid;
  stopCgi(session);
  session.cgi_pid = -1;

  if (timedOut)
    kill(pid, SIGKILL);
  int status = 0;
  pid_t reaped = waitpid(pid, &status, WNOHANG);
  if (reaped == 0)
    _pendingChildren.push_back(std::make_pair(pid, std::time(NULL)));

  std::string response;
  if (timedOut)
    response = errorResponse(504);
  else if ((reaped == pid && WIFEXITED(status) && WEXITSTATUS(status) == 127) ||
           session.cgi_output.empty())
    response = errorResponse(502);
  else
    response = Parsercgi::buildResponse(session.cgi_output);
  std::string().swap(session.cgi_output);
  queueResponse(clientFd, response);
}

void Socket::handleCgiWrite(int pipeFd) {
  ClientSession &session = active_clients[_cgiPipes[pipeFd]];
  size_t left = session.cgi_body.size() - session.cgi_written;
  ssize_t n = write(pipeFd, session.cgi_body.data() + session.cgi_written,
                    std::min(left, static_cast<size_t>(BUFFER_SIZE)));
  if (n > 0)
    session.cgi_written += static_cast<size_t>(n);
  if (n <= 0 || session.cgi_written >= session.cgi_body.size()) {
    unwatchFd(pipeFd);
    _cgiPipes.erase(pipeFd);
    close(pipeFd);
    session.cgi_in = -1;
    std::string().swap(session.cgi_body);
  }
}

void Socket::handleCgiRead(int pipeFd) {
  int clientFd = _cgiPipes[pipeFd];
  char buf[BUFFER_SIZE];
  ssize_t n = read(pipeFd, buf, sizeof(buf));
  if (n > 0) {
    active_clients[clientFd].cgi_output.append(buf, n);
    return;
  }
  finishCgi(clientFd, false);
}

void Socket::sweepTimeouts() {
  time_t now = std::time(NULL);
  std::vector<int> cgiTimedOut;
  std::vector<int> readTimedOut;
  std::vector<int> stalled;

  for (std::map<int, ClientSession>::iterator it = active_clients.begin();
       it != active_clients.end(); ++it) {
    ClientSession &s = it->second;
    if (s.cgi_pid != -1) {
      if (now - s.cgi_start >= CGI_TIMEOUT_SEC)
        cgiTimedOut.push_back(it->first);
    } else if (_readTimeoutSec > 0 && now - s.last_activity >= _readTimeoutSec) {
      if (s.is_response_ready)
        stalled.push_back(it->first);
      else
        readTimedOut.push_back(it->first);
    }
  }

  for (size_t i = 0; i < cgiTimedOut.size(); ++i)
    finishCgi(cgiTimedOut[i], true);
  for (size_t i = 0; i < readTimedOut.size(); ++i)
    queueResponse(readTimedOut[i], errorResponse(408));
  for (size_t i = 0; i < stalled.size(); ++i)
    closeClient(stalled[i]);
}

void Socket::reapChildren() {
  time_t now = std::time(NULL);
  for (size_t i = 0; i < _pendingChildren.size();) {
    pid_t pid = _pendingChildren[i].first;
    if (waitpid(pid, NULL, WNOHANG) != 0) {
      _pendingChildren.erase(_pendingChildren.begin() + i);
      continue;
    }
    if (now - _pendingChildren[i].second >= CGI_TIMEOUT_SEC)
      kill(pid, SIGKILL);
    ++i;
  }
}

void Socket::acceptNewClient() {
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);
  int client_fd = accept(_fd, (struct sockaddr *)&client_addr, &client_len);
  if (client_fd == -1)
    return;

  if (!setNonBlocking(client_fd) || !watchFd(client_fd, this, EPOLLIN)) {
    close(client_fd);
    return;
  }

  ClientSession session;
  session.last_activity = std::time(NULL);
  active_clients[client_fd] = session;
}

bool Socket::rejectIfTooLarge(ClientSession &session, int fd) {
  long effectiveMax = effectiveMaxFor(session.read_buffer);
  if (effectiveMax <= 0)
    return false;

  bool tooLarge;
  if (ParseInputRequest::isChunked(session.read_buffer)) {
    size_t decoded = ParseInputRequest::decodedChunkedSize(session.read_buffer);
    tooLarge = static_cast<long>(decoded) > effectiveMax;
  } else {
    long cl = ParseInputRequest().getContentLengthFromRaw(session.read_buffer);
    tooLarge = cl > effectiveMax;
  }
  if (!tooLarge)
    return false;

  queueResponse(fd, errorResponse(413));
  return true;
}

void Socket::handleClientRead(int fd) {
  char temp_buffer[BUFFER_SIZE];
  ssize_t bytes_recv = recv(fd, temp_buffer, sizeof(temp_buffer), 0);
  if (bytes_recv <= 0) {
    closeClient(fd);
    return;
  }

  ClientSession &session = active_clients[fd];
  if (session.is_response_ready || session.cgi_pid != -1)
    return;
  session.read_buffer.append(temp_buffer, bytes_recv);
  session.last_activity = std::time(NULL);

  size_t headerEnd = session.read_buffer.find("\r\n\r\n");
  size_t headerSize =
      (headerEnd == std::string::npos) ? session.read_buffer.size() : headerEnd;
  if (headerSize > MAX_HEADER_SIZE) {
    queueResponse(fd, errorResponse(431));
    return;
  }
  if (headerEnd == std::string::npos)
    return;
  if (rejectIfTooLarge(session, fd))
    return;
  if (!ParseInputRequest().is_request_complete(session.read_buffer))
    return;

  std::string response = routeRequest(fd, session.read_buffer);
  std::string().swap(session.read_buffer);
  if (!response.empty())
    queueResponse(fd, response);
}

void Socket::handleClientWrite(int fd) {
  std::map<int, ClientSession>::iterator it = active_clients.find(fd);
  if (it == active_clients.end())
    return;

  ClientSession &session = it->second;
  if (!session.is_response_ready)
    return;

  ssize_t bytes_sent =
      send(fd, session.write_buffer.data() + session.write_offset,
           session.write_buffer.size() - session.write_offset, 0);
  if (bytes_sent <= 0) {
    closeClient(fd);
    return;
  }
  session.write_offset += static_cast<size_t>(bytes_sent);
  session.last_activity = std::time(NULL);
  if (session.write_offset >= session.write_buffer.size())
    closeClient(fd);
}

void Socket::handleEvent(int fd, uint32_t events) {
  if (fd == _fd) {
    acceptNewClient();
    return;
  }

  std::map<int, int>::iterator pipe = _cgiPipes.find(fd);
  if (pipe != _cgiPipes.end()) {
    if (fd == active_clients[pipe->second].cgi_in)
      handleCgiWrite(fd);
    else
      handleCgiRead(fd);
    return;
  }

  if (active_clients.find(fd) == active_clients.end())
    return;
  if (events & EPOLLERR) {
    closeClient(fd);
    return;
  }
  if (events & EPOLLIN) {
    handleClientRead(fd);
    if (active_clients.find(fd) == active_clients.end())
      return;
  }
  if (events & EPOLLOUT)
    handleClientWrite(fd);
  else if ((events & EPOLLHUP) && !(events & EPOLLIN))
    closeClient(fd);
}

int Socket::runServers(std::vector<Socket *> &servers) {
  _epollFd = epoll_create(1);
  if (_epollFd == -1) {
    std::cerr << "error when create epoll" << std::endl;
    return -1;
  }
  for (size_t i = 0; i < servers.size(); ++i) {
    if (!watchFd(servers[i]->_fd, servers[i], EPOLLIN)) {
      std::cerr << "error when create async socket" << std::endl;
      close(_epollFd);
      return -1;
    }
  }

  struct epoll_event events[MAX_EVENTS];
  time_t lastSweep = 0;
  while (true) {
    int nfds = epoll_wait(_epollFd, events, MAX_EVENTS, 1000);
    for (int i = 0; i < nfds; ++i) {
      int fd = events[i].data.fd;
      std::map<int, Socket *>::iterator owner = _fdOwner.find(fd);
      if (owner != _fdOwner.end())
        owner->second->handleEvent(fd, events[i].events);
    }

    time_t now = std::time(NULL);
    if (now != lastSweep) {
      for (size_t i = 0; i < servers.size(); ++i)
        servers[i]->sweepTimeouts();
      reapChildren();
      lastSweep = now;
    }
  }
  return 0;
}

void Socket::closeSocket() {
  if (this->_fd != -1) {
    close(this->_fd);
    this->_fd = -1;
  }
}

int Socket::getFd() const { return _fd; }
e_socket_state Socket::getState() const { return _state; }
void Socket::setState(e_socket_state state) { _state = state; }
std::string &Socket::getReadBuffer() { return _readBuffer; }
void Socket::setWriteBuffer(const std::string &response) {
  _writeBuffer = response;
}
