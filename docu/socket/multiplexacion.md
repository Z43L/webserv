# multiplexacion 8-7-2026

**Creado:** 7/8/2026, 10:51:07 AM
**Actualizado:** 7/8/2026, 10:53:41 AM

---

## 👤 **Usuario** (*10:51:07 AM*)

explica como funciona la multiplexacion aqui

## 🤖 **Asistente** (*10:51:07 AM*)

Basado en el contexto que compartes (la clase `Socket`, el objetivo de hacer un "servidor estilo Nginx" y la inclusión de cabeceras de parseo), te explico cómo encajaría y funcionaría la **multiplexación** en este diseño.

Aunque no veo el código exacto del multiplexor (como `select`, `poll`, `epoll` en Linux o `kqueue` en macOS), la estructura de tu clase `Socket` está preparada perfectamente para él.

### ¿Qué es la multiplexación aquí?
En un servidor web, si un cliente se conecta pero no envía datos inmediatamente, un servidor tradicional se quedaría "bloqueado" esperando. Si vienen 1,000 clientes, necesitarías 1,000 hilos para atenderlos. 

La multiplexación (el núcleo de la arquitectura de Nginx) resuelve esto: permite que **un solo hilo/proceso supervise miles de conexiones (`_fd`)** al mismo tiempo. El servidor se lo dice al sistema operativo: *"Avísame solo cuando uno de estos file descriptors tenga datos para leer o esté listo para escribir"*.

### ¿Cómo se relaciona con el código que tienes?

1. **El File Descriptor (`_fd`)**:
   En tu constructor `Socket(int fd, struct sockaddr_in addr)`, guardas el descriptor de archivo. Este número entero es exactamente lo que un sistema de multiplexación monitoriza. El servidor mete todos los `_fd` activos en una lista y se la pasa a funciones como `poll()` o `epoll_wait()`.

2. **La máquina de estados (`state = SOCKET_READING`)**:
   Esta es la clave de la multiplexación. Como el servidor no se bloquea, cuando el sistema operativo dice *"¡Oye, hay datos en el socket 5!"*, el servidor necesita saber en qué estaba trabajando ese socket. 
   - Al crear el socket del cliente, le pones `SOCKET_READING`.
   - El sistema operativo avisa que hay datos -> el servidor lee el HTTP Request -> usa tu clase `ParseInputRequest` para procesarlo.
   - Una vez procesado, el servidor cambiaría el estado a algo como `SOCKET_WRITING` (o `RESPONDING`).
   - El sistema operativo avisa que se puede escribir -> el servidor envía la respuesta HTTP y limpia o cierra el socket.

3. **El socket servidor vs sockets clientes**:
   - El `bindAndListen` crea el socket principal que escucha nuevas conexiones.
   - Este socket principal también se añade al multiplexor. Cuando el OS avisa que hay actividad en el socket principal, significa que un nuevo cliente quiere entrar. El servidor hace `accept()`, lo cual genera un *nuevo* `_fd`, crea un objeto `Socket` con estado `SOCKET_READING`, y añade ese nuevo `_fd` al multiplexor.

### Flujo conceptual de tu servidor con multiplexación:

1. **Inicio:** `Socket.bindAndListen()` deja el puerto listo. Se añade ese `_fd` al sistema de multiplexación.
2. **Evento de conexión:** El multiplexor avisa. Se hace `accept()`. Se crea tu objeto `Socket` con `state = SOCKET_READING` y su nuevo `_fd` se añade al multiplexor.
3. **Evento de lectura:** El cliente manda la petición HTTP (GET, POST...). El multiplexor avisa. El servidor lee el buffer de ese `_fd`. Pasa la data a `ParseInputRequest`.
4. **Evento de escritura:** El servidor genera la respuesta. El multiplexor avisa que el canal está libre para escribir. El servidor manda la data. 
5. Se reinicia el ciclo (Keep-Alive) o se cierra la conexión y se saca el `_fd` del multiplexor.

En resumen, tu clase `Socket` actúa como la "memoria" a corto plazo de cada conexión, mientras que el sistema de multiplexación actúa como el "centinente" que despierta a tu servidor solo cuando algo ocurre en esos sockets.

## 👤 **Usuario** (*10:53:41 AM*)

// Error de lectura o socket vacío (EAGAIN/EWOULDBLOCK)                         // NOTA: El sujeto prohibe ajustar lógica del servidor mediante errno tras read/write [5] como seria esto

## 🤖 **Asistente** (*10:53:41 AM*)

# Cómo sería manejar `EAGAIN/EWOULDBLOCK` sin usar `errno`

## Qué significa el comentario

El comentario está avisando de dos cosas juntas:

1. **`EAGAIN`/`EWOULDBLOCK`** no son errores reales: significan *"ahora mismo no hay nada que leer/escribir, pero el socket está fino"*. Es el comportamiento normal en un socket no bloqueante (`O_NONBLOCK`) cuando la cola de recepción/envío está vacía o llena.

2. La regla **[5]** prohíbe que uses `errno` para tomar decisiones de control de flujo después de `read`/`write` (típica de los enunciados tipo *webserv/42*). Es decir, **esto NO se puede hacer**:

```cpp
// ❌ PROHIBIDO por la regla [5]
int n = recv(_fd, buf, sizeof(buf), 0);
if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    // reintentar / reprogramar lectura
}
```

¿Por qué se prohíbe? Porque en un servidor event-driven (con `poll`/`epoll`/`kqueue`) la **fuente de verdad es el bucle de eventos**, no `errno`. Confías en:

- El **valor de retorno** de `recv`/`send` (`n`).
- El **estado de los eventos** que te notificó `poll`/`epoll` (`POLLIN`, `POLLOUT`, `POLLHUP`, `POLLERR`).

## Cómo sería entonces (forma correcta)

La idea es: cuando `recv` devuelve un valor `<= 0`, paras de leer y dejas que el multiplexor te avise de nuevo más tarde. No insistas, no mires `errno`.

```cpp
void Socket::onReadable() {
    char buf[4096];
    ssize_t n = ::recv(_fd, buf, sizeof(buf), 0);

    if (n > 0) {
        // Se leyeron n bytes: los encolamos en el buffer de recepción
        _recvBuf.append(buf, n);
        // Volvemos a registrar el fd en el poller con POLLIN,
        // para que nos avise si llegan más datos.
        return;
    }

    if (n == 0) {
        // El peer cerró la conexión limpiamente.
        // No usamos errno: el propio valor 0 ya lo dice.
        state = SOCKET_CLOSING;
        return;
    }

    // n < 0: aquí es donde NO consultamos errno.
    // Asumimos que es EAGAIN/EWOULDBLOCK porque el fd es no bloqueante
    // y el poller nos garantizó que había POLLIN; si no había nada
    // listo, simplemente no hacemos nada y esperamos al siguiente evento.
    // (Cualquier error grave se reflejará en el siguiente ciclo como POLLERR/POLLHUP.)
    return;
}
```

Y para el lado de escritura, igual:

```cpp
void Socket::onWritable() {
    ssize_t n = ::send(_fd, _sendBuf.data(), _sendBuf.size(), 0);

    if (n > 0) {
        _sendBuf.erase(0, n);
        if (_sendBuf.empty()) {
            // Ya no hay nada que escribir: dejamos de pedir POLLOUT
            // y volvemos a escuchar solo POLLIN.
            poller->update(_fd, POLLIN);
        }
        return;
    }

    if (n == 0) {
        // Situación extraña, tratada como cierre.
        state = SOCKET_CLOSING;
        return;
    }

    // n < 0: no consultamos errno. El buffer de envío del kernel
    // probablemente está lleno; esperaremos al próximo POLLOUT.
    return;
}
```

## El bucle de eventos decide, no `errno`

El flujo completo "como sería" en el servidor:

```cpp
void Server::run() {
    while (true) {
        int ready = ::poll(_pfds.data(), _pfds.size(), -1);
        // ... tratamiento de errores de poll sí permitido ...

        for (auto& pfd : _pfds) {
            Socket* s = _sockets[pfd.fd];

            if (pfd.revents & (POLLERR | POLLHUP)) {
                s->state = SOCKET_CLOSING;     // error/cierre -> sin errno
                continue;
            }
            if (pfd.revents & POLLIN)  s->onReadable();   // hay datos
            if (pfd.revents & POLLOUT) s->onWritable();   // se puede escribir
        }
        // ... limpiar sockets en SOCKET_CLOSING ...
    }
}
```

## Resumen

| Situación | Prohibido (regla [5]) | Cómo se hace |
|---|---|---|
| Socket vacío / buffer lleno | `if (errno == EAGAIN)` | Mirar `recv()` `<= 0` y **volver a esperar el evento** del poller |
| Error grave | `strerror(errno)` | Mirar `revents & (POLLERR \| POLLHUP)` o retorno `< 0` reiterado |
| Decidir si cierro | por `errno` | por `recv() == 0` o por `POLLHUP/POLLERR` |

Así que "eso" a lo que se refiere el comentario es: **tratar `EAGAIN/EWOULDBLOCK` no consultando `errno`, sino simplemente deteniendo la lectura/escritura y dejando que `poll`/`epoll` te vuelva a avisar cuando el socket esté listo otra vez**.

