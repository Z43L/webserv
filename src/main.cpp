#include "sockets-includes/socket.hpp"
#include "configurationFileParse.hpp"
#include <iostream>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc > 2) {
    std::cerr << "Usage: " << argv[0] << " [config_file]" << std::endl;
    return 1;
  }
  std::string configPath = (argc == 2) ? argv[1] : "confile.conf";

  ConfigParser parser;
  if (!parser.parseFile(configPath)) {
    std::cerr << "Configuration error: " << parser.getErrorMessage() << std::endl;
    return 1;
  }

  const std::vector<ServerConfig> &servers = parser.getServers();
  if (servers.empty()) {
    std::cerr << "Configuration error: no server blocks defined" << std::endl;
    return 1;
  }
  if (servers.size() > 1) {
    std::cerr << "Warning: multiple server blocks found; only the first is bound "
                  "(multi-server support is a follow-up)" << std::endl;
  }
  const ServerConfig &cfg = servers[0];

  Socket server;
  server.setDocRoot(cfg.getRoot());
  server.setIndexFile(cfg.getIndex());
  server.setLocations(cfg.getLocations());
  server.setErrorPages(cfg.getErrorPages());
  server.setMaxBodySize(cfg.getClientMaxBodySize());

  int listenFd = server.bindAndListen(cfg.getHost(), cfg.getListenPort(), 128);
  if (listenFd == -1) {
    std::cerr << "No se pudo iniciar el servidor en " << cfg.getHost() << ":"
              << cfg.getListenPort() << std::endl;
    return 1;
  }

  server.initMonohilo(listenFd);

  server.closeSocket();
  return 0;
}