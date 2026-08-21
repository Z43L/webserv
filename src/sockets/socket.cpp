#include "../sockets-includes/socket.hpp"
#include "../request/parseInputRequest.hpp"
#include "../response/parseResponse.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

Socket::Socket()
    : _fd(-1), _port(0), _ip(""), _state(SOCKET_LISTENING),
      _isNonBlocking(false), _docRoot("./web"), _indexFile("index.html") {}

Socket::Socket(int fd, struct sockaddr_in addr)
    : _fd(fd), _port(0), _ip(""), _state(SOCKET_READING), _isNonBlocking(true),
      addr(addr), _docRoot("./web"), _indexFile("index.html") {}

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
  // "" o "0.0.0.0" siguen significando "escuchar en todas las interfaces";
  // cualquier otra IP se resuelve con inet_pton en vez de ignorarla.
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

      // Caso A: Actividad en el socket de escucha (Nueva Conexión)
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
        client_ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
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

            if (ParseInputRequest().is_request_complete(session.read_buffer)) {
              ParseInputRequest parser;
              parser.parse(session.read_buffer);

              std::string url = parser.getUrl();

              // _indexFile viene de "index" en confile.conf (por defecto "index.html")
              if (url.empty() || url == "/") {
                url = "/" + this->_indexFile;
              }

              // Limpiar query string y fragmento
              size_t qmark = url.find('?');
              if (qmark != std::string::npos) {
                url = url.substr(0, qmark);
              }
              size_t hash = url.find('#');
              if (hash != std::string::npos) {
                url = url.substr(0, hash);
              }

              // Construir ruta física: _docRoot ("root" en confile.conf) + url
              std::string filePath = this->_docRoot + url;
              std::string notFoundPath = this->_docRoot + "/404.html";

              if (filePath.find("..") != std::string::npos) {
                filePath = notFoundPath;
              }

              ParseResponse responseBuilder;
              responseBuilder.setHeader("Server", "webserv/1.0");

              if (ParseResponse::fileExists(filePath)) {
                responseBuilder.setBodyFromFile(filePath);
              } else {
                if (ParseResponse::fileExists(notFoundPath)) {
                  responseBuilder.setStatus(404, "Not Found");
                  responseBuilder.setBodyFromFile(notFoundPath);
                } else {
                  responseBuilder.setStatus(404, "Not Found");
                  responseBuilder.setBody(
                      "<html><body><h1>404 Not Found</h1></body></html>");
                }
              }

              session.write_buffer = responseBuilder.build();
              session.is_response_ready = true;
              session.read_buffer.clear();
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
void Socket::setDocRoot(const std::string &docRoot) { _docRoot = docRoot; }
void Socket::setIndexFile(const std::string &indexFile) { _indexFile = indexFile; }
