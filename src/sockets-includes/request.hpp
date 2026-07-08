#ifndef REQUEST_TPP
#define REQUEST_TPP
#include "../server.hpp"
template <typename T>
class request : public ParseInputRequest, public Securiti {
    private:
        const T& _serverConfig; // Ejemplo de uso del tipo template T (p. ej., Configuración del Servidor)

    public:
        request(const T& config);
        ~request();

       
        bool isRouterCorrect(const std::string& url);
};

// Incluimos el archivo de plantilla con las implementaciones (etiqueta requerida en Webserv)
#include "Request.tpp"