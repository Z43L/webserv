#ifndef PARSERCGI_HPP
#define PARSERCGI_HPP

#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#define CGI_TIMEOUT_SEC 30

struct CgiProcess {
  pid_t pid;
  int inFd;
  int outFd;
};

class Parsercgi {
public:
  Parsercgi() {}
  ~Parsercgi() {}

  static bool start(const std::string &interpreter,
                    const std::string &scriptPath,
                    const std::string &rawRequest,
                    const std::string &queryString,
                    size_t bodyLength,
                    const std::string &serverHost,
                    int serverPort,
                    const std::vector<int> &fdsToClose,
                    CgiProcess &out);

  static std::string buildResponse(const std::string &cgiOutput);
};

#endif
