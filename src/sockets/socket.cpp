#include "../sockets-includes/socket.hpp"
#include "../request/parseInputRequest.hpp"
#include "../response/parseResponse.hpp"
#include "../cgi-includes/parserCgi.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

Socket::Socket()
    : _fd(-1), _port(0), _ip(""), _state(SOCKET_LISTENING),
      _isNonBlocking(false), _docRoot("./web"), _indexFile("index.html"),
      _maxBodySize(1048576) {}

Socket::Socket(int fd, struct sockaddr_in addr)
    : _fd(fd), _port(0), _ip(""), _state(SOCKET_READING), _isNonBlocking(true),
      addr(addr), _docRoot("./web"), _indexFile("index.html"),
      _maxBodySize(1048576) {}

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

// Returns the location with the longest matching prefix. Empty LocationBlock
// (path == "") means "no location matched, use server-level directives".
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

// Build the on-disk path for url under the active location (alias replaces
// the prefix; root appends). Returns "" if no location matched.
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

std::string Socket::routeRequest(const std::string &rawRequest) {
  ParseInputRequest parser;
  parser.parse(rawRequest);

  std::string method = parser.getMethod();
  std::string url = parser.getUrl();
  std::string queryString;
  size_t q = url.find('?');
  if (q != std::string::npos) {
    queryString = url.substr(q + 1);
    url = url.substr(0, q);
  }

  if (method != "GET" && method != "POST" && method != "DELETE" &&
      method != "PUT" && method != "HEAD") {
    ParseResponse r;
    r.setStatus(405, "Method Not Allowed");
    r.setHeader("Allow", "GET, POST, DELETE, PUT, HEAD");
    r.setBody("");
    return r.build();
  }

  LocationBlock loc = matchLocation(_locations, url);

  // max_body enforcement: a location can override the server-wide limit
  // (client_max_body_size -1 means "inherit from server"); a value of 0
  // means "no limit". Use whichever is in scope when validating the body.
  long effectiveMax = _maxBodySize;
  if (loc.client_max_body_size >= 0)
    effectiveMax = loc.client_max_body_size;

  // max_body enforcement: Content-Length must not exceed client_max_body_size.
  long declaredLen = parser.getContentLength();
  if (effectiveMax > 0 && declaredLen > effectiveMax) {
    ParseResponse r;
    r.setStatus(413, "Payload Too Large");
    r.setBody("");
    return r.build();
  }

  size_t bodyStart = rawRequest.find("\r\n\r\n");
  size_t bodyLen =
      (bodyStart == std::string::npos) ? 0 : rawRequest.size() - bodyStart - 4;
  if (effectiveMax > 0 && static_cast<long>(bodyLen) > effectiveMax) {
    ParseResponse r;
    r.setStatus(413, "Payload Too Large");
    r.setBody("");
    return r.build();
  }


  std::vector<std::string> methods = loc.allow_methods;
  if (methods.empty()) {
    methods.push_back("GET");
  }
  if (!methodAllowed(methods, method)) {
    ParseResponse r;
    r.setStatus(405, "Method Not Allowed");
    std::string allow;
    for (size_t i = 0; i < methods.size(); ++i) {
      if (i)
        allow += ", ";
      allow += methods[i];
    }
    r.setHeader("Allow", allow);
    r.setBody("");
    return r.build();
  }

  // `return` directive: redirect.
  if (!loc.return_path.empty()) {
    ParseResponse r;
    r.setStatus(302, "Found");
    r.setHeader("Location", loc.return_path);
    r.setBody("");
    return r.build();
  }

  // CGI dispatch: if method is POST and the URL extension is in the
  // location's cgi_ext list, run the matching interpreter with the file
  // under the location's alias/root.
  if (method == "POST" && !loc.cgi_ext.empty()) {
    for (size_t i = 0; i < loc.cgi_ext.size(); ++i) {
      const std::string &ext = loc.cgi_ext[i];
      if (endsWith(url, ext)) {
        std::string filePath = resolvePath(url, loc, _docRoot);
        // interpreter is the i-th entry in cgi_path (paired by index).
        std::string interpreter;
        if (i < loc.cgi_path.size())
          interpreter = loc.cgi_path[i];
        else if (!loc.cgi_path.empty())
          interpreter = loc.cgi_path[0];
        Parsercgi cgi;
        return cgi.execute(interpreter, filePath, rawRequest, queryString,
                           _ip, _port);
      }
    }
  }

  // File serving.
  std::string filePath = resolvePath(url, loc, _docRoot);
  if (filePath.find("..") != std::string::npos) {
    ParseResponse r;
    r.setStatus(403, "Forbidden");
    r.setBody("");
    return r.build();
  }

  // Directory handling: look for the configured index file inside the
  // directory; if missing and the request is for the location root, fall
  // back to autoindex (only when explicitly enabled for this location).
  // For sub-directories without an index file we return 404: the user asked
  // for `<dir>/<sub>` and that path resolves to a directory with no
  // indexable entry, so silently listing the contents is not what was asked.
  if (isDirectory(filePath)) {
    std::string indexFile = !loc.index.empty() ? loc.index : _indexFile;
    std::string withIndex = filePath;
    if (!withIndex.empty() && withIndex[withIndex.size() - 1] != '/')
      withIndex += '/';
    withIndex += indexFile;
    if (ParseResponse::fileExists(withIndex)) {
      filePath = withIndex;
    } else if (loc.autoindex &&
               (url == loc.path || url + "/" == loc.path ||
                url == loc.path + "/" || url == loc.path)) {
      std::string urlPath = url;
      if (urlPath.empty() || urlPath[urlPath.size() - 1] != '/')
        urlPath += '/';
      std::string body = generateAutoindex(filePath, urlPath);
      ParseResponse r;
      r.setStatus(200, "OK");
      r.setHeader("Content-Type", "text/html");
      r.setBody(body);
      return r.build();
    } else {
      ParseResponse r;
      r.setStatus(404, "Not Found");
      r.setBody("");
      return r.build();
    }
  }

  if (!ParseResponse::fileExists(filePath)) {
    ParseResponse r;
    r.setStatus(404, "Not Found");
    for (size_t i = 0; i < _errorPages.size(); ++i) {
      if (_errorPages[i].code == 404) {
        std::string p = _docRoot + _errorPages[i].path;
        if (ParseResponse::fileExists(p))
          r.setBodyFromFile(p);
        else
          r.setBody("");
        return r.build();
      }
    }
    r.setBody("");
    return r.build();
  }

  if (method == "HEAD") {
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

  ParseResponse r;
  r.setBodyFromFile(filePath);
  return r.build();
}

std::string Socket::handleReadRequest(const std::string &rawRequest) {
  return routeRequest(rawRequest);
}

int Socket::initMonohilo(int listenFd) {
  int epollFd = epoll_create(1);
  if (epollFd == -1) {
    close(listenFd);
    std::cout << "error when create epoll" << std::endl;
    return -1;
  }

  struct epoll_event ev;
  std::memset(&ev, 0, sizeof(ev));
  ev.events = EPOLLIN;
  ev.data.fd = listenFd;

  if (epoll_ctl(epollFd, EPOLL_CTL_ADD, listenFd, &ev) == -1) {
    std::cout << "error when create async socket" << std::endl;
    close(epollFd);
    return -1;
  }

  while (true) {
    int nfds = epoll_wait(epollFd, events, MAX_EVENTS, -1);
    if (nfds == -1) {
      std::cerr << "Error en epoll_wait" << std::endl;
      continue;
    }

    for (int i = 0; i < nfds; ++i) {
      int currentFd = events[i].data.fd;

      if (currentFd == listenFd) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd =
            accept(listenFd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) {
          continue;
        }

        if (!setNonBlocking(client_fd)) {
          close(client_fd);
          continue;
        }

        struct epoll_event client_ev;
        std::memset(&client_ev, 0, sizeof(client_ev));
        client_ev.events = EPOLLIN;
        client_ev.data.fd = client_fd;

        if (epoll_ctl(epollFd, EPOLL_CTL_ADD, client_fd, &client_ev) == -1) {
          close(client_fd);
          continue;
        }

        ClientSession session;
        session.is_response_ready = false;
        active_clients[client_fd] = session;
        std::cout << "Nueva conexión aceptada. Socket FD: " << client_fd
                  << std::endl;
      } else {
        if (events[i].events & EPOLLIN) {
          char temp_buffer[BUFFER_SIZE];
          ssize_t bytes_recv =
              recv(currentFd, temp_buffer, sizeof(temp_buffer) - 1, 0);

          if (bytes_recv > 0) {
            temp_buffer[bytes_recv] = '\0';
            ClientSession &session = active_clients[currentFd];
            session.read_buffer.append(temp_buffer, bytes_recv);

            // If the headers have arrived and Content-Length already exceeds
            // the server-wide limit, reject immediately rather than waiting
            // for the (possibly never-arriving) full body. The per-location
            // override is applied later in routeRequest, so this early
            // reject only fires when the operator has set a server-level
            // cap that the request would clearly violate.
            if (session.write_buffer.empty() && _maxBodySize > 0 &&
                session.read_buffer.find("\r\n\r\n") != std::string::npos) {
              if (ParseInputRequest::isChunked(session.read_buffer)) {
                size_t decoded = ParseInputRequest::decodedChunkedSize(
                    session.read_buffer);
                if (static_cast<long>(decoded) > _maxBodySize) {
                  ParseResponse r;
                  r.setStatus(413, "Payload Too Large");
                  session.write_buffer = r.build();
                  session.is_response_ready = true;
                  struct epoll_event client_ev;
                  std::memset(&client_ev, 0, sizeof(client_ev));
                  client_ev.events = EPOLLIN | EPOLLOUT;
                  client_ev.data.fd = currentFd;
                  epoll_ctl(epollFd, EPOLL_CTL_MOD, currentFd, &client_ev);
                  continue;
                }
              } else {
                long cl = ParseInputRequest().getContentLengthFromRaw(
                    session.read_buffer);
                if (cl > _maxBodySize) {
                  ParseResponse r;
                  r.setStatus(413, "Payload Too Large");
                  session.write_buffer = r.build();
                  session.is_response_ready = true;
                  struct epoll_event client_ev;
                  std::memset(&client_ev, 0, sizeof(client_ev));
                  client_ev.events = EPOLLIN | EPOLLOUT;
                  client_ev.data.fd = currentFd;
                  epoll_ctl(epollFd, EPOLL_CTL_MOD, currentFd, &client_ev);
                  continue;
                }
              }
            }

            if (ParseInputRequest().is_request_complete(session.read_buffer)) {
              session.write_buffer = handleReadRequest(session.read_buffer);
              session.is_response_ready = true;
              session.read_buffer.clear();

              struct epoll_event client_ev;
              std::memset(&client_ev, 0, sizeof(client_ev));
              client_ev.events = EPOLLIN | EPOLLOUT;
              client_ev.data.fd = currentFd;
              epoll_ctl(epollFd, EPOLL_CTL_MOD, currentFd, &client_ev);
            }
          } else if (bytes_recv == 0) {
            epoll_ctl(epollFd, EPOLL_CTL_DEL, currentFd, NULL);
            close(currentFd);
            active_clients.erase(currentFd);
            continue;
          } else {
            std::cout << "error de lectura " << std::endl;
          }
        }

        if (events[i].events & EPOLLOUT) {
          std::map<int, ClientSession>::iterator it =
              active_clients.find(currentFd);
          if (it != active_clients.end()) {
            ClientSession &session = it->second;

            if (session.is_response_ready && !session.write_buffer.empty()) {
              ssize_t bytes_sent = send(currentFd, session.write_buffer.c_str(),
                                        session.write_buffer.size(), 0);

              if (bytes_sent > 0) {
                session.write_buffer.erase(0, bytes_sent);

                if (session.write_buffer.empty()) {
                  std::cout
                      << "Respuesta HTTP enviada con éxito al FD: " << currentFd
                      << std::endl;
                  epoll_ctl(epollFd, EPOLL_CTL_DEL, currentFd, NULL);
                  close(currentFd);
                  active_clients.erase(it);
                }
              }
            }
          }
        }
      }
    }
  }
  close(listenFd);
  close(epollFd);
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