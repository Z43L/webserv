#include "sockets-includes/socket.hpp"
#include "configurationFileParse.hpp"
#include <csignal>
#include <iostream>
#include <unistd.h>

int main(int argc, char **argv) {
  std::signal(SIGPIPE, SIG_IGN);

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
  std::vector<Socket *> sockets;
  for (size_t i = 0; i < servers.size(); ++i) {
    const ServerConfig &cfg = servers[i];
    Socket *server = new Socket();
    server->setDocRoot(cfg.getRoot());
    server->setIndexFile(cfg.getIndex());
    server->setLocations(cfg.getLocations());
    server->setErrorPages(cfg.getErrorPages());
    server->setMaxBodySize(cfg.getClientMaxBodySize());
    server->setReadTimeout(cfg.getClientReadTimeoutSeconds());

    if (server->bindAndListen(cfg.getHost(), cfg.getListenPort(), 128) == -1) {
      std::cerr << "No se pudo iniciar el servidor en " << cfg.getHost() << ":"
                << cfg.getListenPort() << std::endl;
      delete server;
      continue;
    }
    std::cout << "Escuchando en " << cfg.getHost() << ":"
              << cfg.getListenPort() << std::endl;
    sockets.push_back(server);
  }
  if (sockets.empty())
    return 1;

  int ret = Socket::runServers(sockets);

  for (size_t i = 0; i < sockets.size(); ++i)
    delete sockets[i];
  return ret == 0 ? 0 : 1;
}
