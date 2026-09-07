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

// Segundos que se le conceden a un CGI antes de matarlo y responder 504. Sin
// esto un CGI colgado congela el servidor entero (es monoproceso).
#define CGI_TIMEOUT_SEC 10

class Parsercgi {
public:
  Parsercgi() {}
  ~Parsercgi() {}

  // fdsToClose son los descriptores del servidor (socket de escucha, epoll y
  // conexiones de otros clientes) que el hijo hereda del fork y debe cerrar
  // antes del execve: si no, un CGI colgado mantiene el puerto en LISTEN.
  std::string execute(const std::string &interpreter,
                      const std::string &scriptPath,
                      const std::string &rawRequest,
                      const std::string &queryString,
                      const std::string &serverHost,
                      int serverPort,
                      const std::vector<int> &fdsToClose);
};

#endif