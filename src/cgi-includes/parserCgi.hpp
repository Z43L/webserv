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

#define BUFFER_SIZE 4096

#define CGI_TIMEOUT_SEC 10

class Parsercgi {
public:
  Parsercgi() {}
  ~Parsercgi() {}

  std::string execute(const std::string &interpreter,
                      const std::string &scriptPath,
                      const std::string &rawRequest,
                      const std::string &queryString,
                      const std::string &serverHost,
                      int serverPort,
                      const std::vector<int> &fdsToClose);
};

#endif
