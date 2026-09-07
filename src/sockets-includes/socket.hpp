#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <iostream>
#include <map>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include "confile/serverConfig.hpp"
#define MAX_EVENTS 64
#define BUFFER_SIZE 4096

struct ClientSession {
  std::string read_buffer;
  std::string write_buffer;
  bool is_response_ready;
};

enum e_socket_state {
  SOCKET_LISTENING,
  SOCKET_READING,
  SOCKET_PROCESSING,
  SOCKET_WRITING,
  SOCKET_CLOSING
};

class Socket {
private:
  int _fd;
  int _port;
  std::string _ip;
  e_socket_state _state;
  std::string _readBuffer;
  std::string _writeBuffer;
  bool _isNonBlocking;
  struct sockaddr_in addr;
  std::string _docRoot;
  std::string _indexFile;
  std::vector<LocationBlock> _locations;
  std::vector<ErrorPage>     _errorPages;
  long                       _maxBodySize;
  bool setNonBlocking(int fd);

  std::string handleReadRequest(const std::string &rawRequest);
  std::string routeRequest(const std::string &rawRequest);

public:
  Socket();
  Socket(int fd, struct sockaddr_in addr);
  ~Socket();

  int bindAndListen(const std::string &ip, int port, int backlog);
  Socket *acceptConnection();

  void setDocRoot(const std::string &docRoot);
  void setIndexFile(const std::string &indexFile);
  void setLocations(const std::vector<LocationBlock> &locations);
  void setErrorPages(const std::vector<ErrorPage> &errorPages);
  void setMaxBodySize(long size);

  std::map<int, ClientSession> active_clients;
  struct epoll_event events[MAX_EVENTS];
  ssize_t readData();
  ssize_t writeData();
  void closeSocket();
  int initMonohilo(int fd);

  int getFd() const;
  e_socket_state getState() const;
  void setState(e_socket_state state);
  std::string &getReadBuffer();
  void setWriteBuffer(const std::string &response);
};

#endif