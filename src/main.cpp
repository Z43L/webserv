#include "sockets-includes/socket.hpp"
#include <iostream>
#include <unistd.h>
int main(void) {
  int port = 8000;

  Socket server;

  int listenFd = server.bindAndListen("0.0.0.0", port, 128);
  if (listenFd == -1) {
    std::cerr << "No se pudo iniciar el servidor en el puerto " << port
              << std::endl;
    return 1;
  }

  server.initMonohilo(listenFd);

  server.closeSocket();
  return 0;
}
