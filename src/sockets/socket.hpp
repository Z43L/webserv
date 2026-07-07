#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "../server.hpp"

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

    bool setNonBlocking(int fd);

public:
    
    Socket();
    Socket(int fd, struct sockaddr_in addr);
    ~Socket();

    // Métodos para socket de escucha (Listening socket)
    bool bindAndListen(const std::string& ip, int port, int backlog);
    Socket* acceptConnection();

    // Métodos para E/S (Client socket)
    ssize_t readData();   // Lee hacia _readBuffer
    ssize_t writeData();  // Escribe desde _writeBuffer
    void closeSocket();

    
    int getFd() const;
    e_socket_state getState() const;
    void setState(e_socket_state state);
    std::string& getReadBuffer();
    void setWriteBuffer(const std::string& response);
};

#endif // SOCKET_HPP