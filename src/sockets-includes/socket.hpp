#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "../server.hpp"
#define MAX_EVENTS 64
#define BUFFER_SIZE 4096

struct ClientSession {
    std::string read_buffer;
    std::string write_buffer;
    bool        is_response_ready;
};

// Enumeración para el estado de la conexión 
enum e_socket_state {
    SOCKET_LISTENING,  // Esperando nuevas conexiones
    SOCKET_READING,    // Leyendo datos del cliente
    SOCKET_PROCESSING, // El request se está parseando/procesando
    SOCKET_WRITING,    // Enviando la respuesta al cliente
    SOCKET_CLOSING    // Listo para cerrarse
};

class Socket {
private:
    int _fd;                   // File descriptor del socket
    int _port;                 // Puerto de escucha (si es listening socket)
    std::string _ip;           // IP de escucha
    e_socket_state _state;     // Estado actual de la conexión
    std::string _readBuffer;   // Buffer para almacenar lo que llega
    std::string _writeBuffer;  // Buffer para almacenar lo que se va a enviar
    bool _isNonBlocking;       // Bandera de modo no bloqueante
    struct sockaddr_in addr;
    bool setNonBlocking(int fd);

public:
    
    Socket();
    Socket(int fd, struct sockaddr_in addr);
    ~Socket();

    int bindAndListen(const std::string& ip, int port, int backlog);
    Socket* acceptConnection();

    std::map<int, ClientSession> active_clients;
    struct epoll_event events[MAX_EVENTS];
    ssize_t readData(); 
    ssize_t writeData()  
    void closeSocket();
    int initMonohilo(int fd);
    
    int getFd() const;
    e_socket_state getState() const;
    void setState(e_socket_state state);
    std::string& getReadBuffer();
    void setWriteBuffer(const std::string& response);
};

#endif // SOCKET_HPP