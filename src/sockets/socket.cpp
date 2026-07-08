#include "../sever.hpp"

Socket::Socket(){}

Socket::Socket(int fd, struct sockaddr_in addr) : _fd(fd), _addr(addr){
  this->state = SOCKET_READING;
  this->_fd = fd;
  this->addr = addr;
}
Socket::~Socket(){};

int Socket::bindAndListen(const std::string& ip, int port, int backlog)
{
  int fd = socket(AF_INET, SOC_STREAM, 0);
  if(fd == -1)
  {
      std::cout << "error when open socket" << std::endl;
      return -1;
  }
  int opt = 1;
  setsockpt(fd, SOL_SOCKET,SO_REUSEADDR, &opt, sizeof(opt) )
  if (fcntl(server_fd, F_SETFL, O_NONBLOCK) == -1){
    close(fd);
    std::cout << "error when save socket" << std::endl;
    return -1;
  }
  struct sockaddr_in address;
  std::memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(8080);
  if (bind(fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
        std::cerr << "Error en el bind" << std::endl;
        close(fd);
        return -1;
    }

    if (listen(fd, 128) == -1) {
        std::cerr << "Error en el listen" << std::endl;
        close(fd);
        return -1;
    }
    return fd;
}

int Socket::initMonohilo(int fd)
{
   int epollFd = epoll_create(1)
   if( epollFd == -1){
     close(fd);
     std::cout << "error when create epoll" << std::endl;
     return -1;
   }   
   struct epoll_ev ev;
   std::memset(&ev, 0, sizeof(ev));
   ev.events = EPOLLIN;
   ev.data.ev = fd;
   
   if(epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) == -1)
   {
     std::cout << "error when create async socket" << std::endl;
     return -1;
   }
 
   return epollFd;
}

int eventLoop(int fd, int epollFd)
{
   while (true) {
        // Monitorea simultáneamente todos los descriptores registrados
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1); [8]
        if (nfds == -1) {
            std::cerr << "Error en epoll_wait" << std::endl;
            continue;
        }
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            // Caso A: Actividad en el socket de escucha (Nueva Conexión)
            if (fd == server_fd) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len); [2]
                
                if (client_fd == -1) {
                    continue;
                }

                // Configurar socket de cliente como NO BLOQUEANTE
                if (fcntl(client_fd, F_SETFL, O_NONBLOCK) == -1) { [6]
                    close(client_fd);
                    continue;
                }

                // Registrar en epoll monitoreando LECTURA y ESCRITURA simultáneamente [5]
                struct epoll_event client_ev;
                std::memset(&client_ev, 0, sizeof(client_ev));
                // EPOLLIN para recibir peticiones, EPOLLOUT para responder de manera asíncrona [5]
                client_ev.events = EPOLLIN | EPOLLOUT | EPOLLET; 
                client_ev.data.fd = client_fd;

                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) == -1) {
                    close(client_fd);
                    continue;
                }

                // Inicializar sesión del cliente en el mapa
                ClientSession session;
                active_clients[client_fd] = session;
                std::cout << "Nueva conexión aceptada. Socket FD: " << client_fd << std::endl;
            } 
            // Caso B: Actividad en un socket de conexión de cliente
            else {
                // Sub-caso B.1: El socket está listo para LECTURA (Recibir datos)
                if (bytes_recv > 0) {
        // Acumular los bytes leídos en el string de este cliente específico
        _client_buffers[client_fd].append(temp_buffer, bytes_recv);
        
        // Comprobar si ya tenemos la petición completa dentro del búfer acumulado
        std::string& rawInput = _client_buffers[client_fd];
        
        if (is_request_complete(rawInput)) {
            // ¡Aquí ya tienes tu rawInput completo! 
            // Procedemos a parsearlo con tu clase
            ParseInputRequest parser;
            parser.parse(rawInput);
            
            // 1. Procesar la request (enrutar, buscar archivo, ejecutar CGI, etc.)
            // 2. Preparar la respuesta en un búfer de escritura
            // 3. Limpiar o remover los datos procesados de _client_buffers[client_fd]
        }
    } 
                    else {
                        // Error de lectura o socket vacío (EAGAIN/EWOULDBLOCK)
                        // NOTA: El sujeto prohibe ajustar lógica del servidor mediante errno tras read/write [5]
                    }
                }

                // Sub-caso B.2: El socket está listo para ESCRITURA (Enviar datos)
                if (events[i].events & EPOLLOUT) { [5]
                    ClientSession& session = active_clients[fd];
                    
                    // Si tenemos una respuesta lista para enviar y hay datos en el buffer de salida
                    if (session.is_response_ready && !session.write_buffer.empty()) {
                        ssize_t bytes_sent = send(fd, session.write_buffer.c_str(), session.write_buffer.size(), 0);
                        
                        if (bytes_sent > 0) {
                            // Eliminar los bytes enviados con éxito de nuestro buffer
                            session.write_buffer.erase(0, bytes_sent);
                            
                            // Si se ha transmitido toda la respuesta:
                            if (session.write_buffer.empty()) {
                                std::cout << "Respuesta HTTP enviada con éxito al FD: " << fd << std::endl;
                                // Si no es keep-alive, cerrar la conexión de inmediato
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                                close(fd);
                                active_clients.erase(fd);
                            }
                        }
                    }
                }
            }
        }
    }

    close(server_fd);
    close(epoll_fd);
    return 0;

}