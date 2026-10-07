#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <ctime>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include "confile/serverConfig.hpp"
#define MAX_EVENTS 64
#define BUFFER_SIZE 65536
#define MAX_HEADER_SIZE 16384

struct ClientSession {
  ClientSession()
      : write_offset(0), is_response_ready(false), last_activity(0),
        cgi_pid(-1), cgi_in(-1), cgi_out(-1), cgi_written(0), cgi_start(0) {}

  std::string read_buffer;
  std::string write_buffer;
  size_t write_offset;
  bool is_response_ready;
  time_t last_activity;

  pid_t cgi_pid;
  int cgi_in;
  int cgi_out;
  std::string cgi_body;
  size_t cgi_written;
  std::string cgi_output;
  time_t cgi_start;
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
  long                       _readTimeoutSec;
  std::map<int, int>         _cgiPipes;

  static int _epollFd;
  static std::map<int, Socket *> _fdOwner;
  static std::vector<std::pair<pid_t, time_t> > _pendingChildren;

  bool setNonBlocking(int fd);

  std::string routeRequest(int clientFd, const std::string &rawRequest);
  std::string errorResponse(int code, const std::string &headerKey = "",
                            const std::string &headerValue = "") const;

  long effectiveMaxFor(const std::string &rawRequest) const;

  std::string checkBodyLimits(const std::string &rawRequest,
                              long declaredLen) const;
  std::string runCgi(int clientFd, const std::string &url,
                     const LocationBlock &loc, const std::string &rawRequest,
                     const std::string &queryString, size_t extIdx);
  std::string handleUpload(const std::string &url, const LocationBlock &loc,
                           const std::string &rawRequest);
  std::string handleDelete(const std::string &filePath);
  bool resolveDirectory(std::string &filePath, const std::string &url,
                        const LocationBlock &loc, std::string &out);

  static bool watchFd(int fd, Socket *owner, uint32_t events);
  static void changeFd(int fd, uint32_t events);
  static void unwatchFd(int fd);
  static void reapChildren();

  void handleEvent(int fd, uint32_t events);
  void sweepTimeouts();
  void acceptNewClient();
  bool rejectIfTooLarge(ClientSession &session, int fd);
  void handleClientRead(int fd);
  void handleClientWrite(int fd);
  void handleCgiWrite(int pipeFd);
  void handleCgiRead(int pipeFd);
  void finishCgi(int clientFd, bool timedOut);
  void stopCgi(ClientSession &session);
  void queueResponse(int fd, const std::string &response);
  void closeClient(int fd);

public:
  Socket();
  ~Socket();

  int bindAndListen(const std::string &ip, int port, int backlog);

  void setDocRoot(const std::string &docRoot);
  void setIndexFile(const std::string &indexFile);
  void setLocations(const std::vector<LocationBlock> &locations);
  void setErrorPages(const std::vector<ErrorPage> &errorPages);
  void setMaxBodySize(long size);
  void setReadTimeout(long seconds);

  std::map<int, ClientSession> active_clients;
  void closeSocket();

  static int runServers(std::vector<Socket *> &servers);

  int getFd() const;
  e_socket_state getState() const;
  void setState(e_socket_state state);
  std::string &getReadBuffer();
  void setWriteBuffer(const std::string &response);
};

#endif
