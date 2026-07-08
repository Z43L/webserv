# planteamiento 7-7-2026

**Creado:** 7/7/2026, 11:12:52 AM
**Actualizado:** 7/7/2026, 11:12:52 AM

---

## 👤 **Usuario** (*11:12:52 AM*)

como definirias la clase socket  para un servidor web estilo nginx explica como funcionaria el socket dentro del servidor

## 🤖 **Asistente** (*11:12:52 AM*)

Para definir una clase `Socket` en un servidor web estilo Nginx, debemos entender primero cómo funciona Nginx: utiliza una **arquitectura asíncrona, no bloqueante y orientada a eventos** (usualmente con `epoll` en Linux o `kqueue` en BSD).

En este modelo, la clase `Socket` **no debe encargarse de parsear la petición** (como sugiere tu método `parse_input` en el código actual). La responsabilidad del `Socket` es puramente de **E/S (Entrada/Salida) de red** y gestión del estado de la conexión. El parseo se delega a tus clases `ParseInputRequest` y `Securiti`.

Aquí tienes cómo redefiniría la clase `Socket` y cómo funcionaría dentro del servidor.

### 1. Definición de la clase `Socket`

```cpp
#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "../server.hpp"

// Enumeración para el estado de la conexión (Estilo Nginx)
enum e_socket_state {
    SOCKET_LISTENING,  // Esperando nuevas conexiones
    SOCKET_READING,    // Leyendo datos del cliente
    SOCKET_PROCESSING, // El request se está parseando/procesando
    SOCKET_WRITING,    // Enviando la respuesta al cliente
    SOCKET_CLOSING    // Listo para cerrarse
};

template <typename T>
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
    // Constructores para sockets de escucha y de cliente
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

    // Getters y Setters
    int getFd() const;
    e_socket_state getState() const;
    void setState(e_socket_state state);
    std::string& getReadBuffer();
    void setWriteBuffer(const std::string& response);
};

#endif // SOCKET_HPP
```

### 2. ¿Cómo funcionaría el Socket dentro del servidor?

El servidor Nginx funciona con un **Event Loop** (Bucle de eventos). El `Socket` no hace nada por sí solo hasta que el sistema operativo le dice al servidor que hay actividad en ese File Descriptor (`_fd`).

El flujo sería el siguiente:

#### Fase A: Inicialización
1. El servidor arranca y crea un `Socket` maestro (Listener) llamando a `bindAndListen()`.
2. Este socket maestro se configura como **no bloqueante** (`setNonBlocking`) y se registra en el sistema de eventos (por ejemplo, `epoll_ctl` en Linux).
3. Su estado es `SOCKET_LISTENING`.

#### Fase B: Bucle de Eventos (Event Loop)
El servidor entra en un bucle infinito esperando eventos (ej. `epoll_wait`):
- **Si el evento ocurre en el Socket Maestro:**
  1. El servidor llama a `acceptConnection()`.
  2. Se crea un nuevo objeto `Socket` para el cliente conectado.
  3. Este nuevo socket también se hace no bloqueante y se registra en `epoll`.
  4. Su estado cambia a `SOCKET_READING`.

- **Si el evento ocurre en un Socket de Cliente (Datos disponibles para leer):**
  1. El servidor llama a `readData()`, que llena el `_readBuffer`.
  2. El estado cambia a `SOCKET_PROCESSING`.
  3. **Aquí es donde interactúas con tus otras clases (`request.hpp`, `parseInputRequest.hpp`, `securiti.hpp`).** El servidor toma el `_readBuffer` del socket y lo pasa a tu clase `ParseInputRequest`. Tu clase `Securiti` analiza el buffer en busca de inyecciones SQL o XSS.
  4. Si el parseo es exitoso y seguro, el servidor genera una respuesta HTTP y la inyecta en el socket usando `setWriteBuffer()`.
  5. El estado cambia a `SOCKET_WRITING`.

- **Si el evento ocurre en un Socket de Cliente (Listo para escribir):**
  1. El servidor llama a `writeData()`.
  2. Si el `_writeBuffer` se vacía por completo, el estado cambia a `SOCKET_CLOSING` (o a `SOCKET_READING` si soporta Keep-Alive, como Nginx).

### 3. Corrección sobre tu diseño actual

En tu archivo actual `socket.hpp`, tenías esto:
```cpp
std::map<std::string T> parse_input(std::map <std::string T> requestParser, parseInputRequest& request )
```

**Recomendación:** Elimina esto de la clase `Socket`. El socket no debe saber qué es un método GET, POST o un map. El socket solo recibe un flujo de bytes (`std::string _readBuffer`). 

La arquitectura ideal, usando tus archivos, sería:
1. `Socket` recibe los bytes en bruto.
2. El `Server` toma el `readBuffer` del socket y se lo pasa a `ParseInputRequest`.
3. `Securiti` valida los datos ya parseados.
4. La clase `request<T>` maneja la lógica de negocio y configura el `_writeBuffer` del `Socket` con la respuesta HTTP correspondiente.

