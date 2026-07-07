template <typename T>
class request : public ParseInputRequest, public Securiti {
    private:
        const T& _serverConfig; // Ejemplo de uso del tipo template T (p. ej., Configuración del Servidor)

    public:
        request(const T& config);
        ~request();

        // Valida si la ruta solicitada coincide con las reglas del archivo de configuración (rules/routes)
        bool isRouterCorrect(const std::string& url);
};

// Incluimos el archivo de plantilla con las implementaciones (etiqueta requerida en Webserv)
#include "Request.tpp"