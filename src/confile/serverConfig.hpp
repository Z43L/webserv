#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <string>
#include <vector>

// One (code, path) pair. A single 'error_page 404 500 /x.html;' directive
// expands into two ErrorPage entries sharing the same path.
struct ErrorPage {
    int         code;
    std::string path;
};

// Mirrors one 'location <path> { ... }' block, including the special
// 'location cgi-bin { ... }' block (cgi_path/cgi_ext are only meaningful there).
struct LocationBlock {
    LocationBlock() : autoindex(false) {}

    std::string path;
    std::string root;
    std::string index;
    bool        autoindex;
    std::vector<std::string> allow_methods;
    std::string return_path;
    std::string alias;
    std::vector<std::string> cgi_path;
    std::vector<std::string> cgi_ext;
};

class ServerConfig {
    public:
        // listen_port stays -1 until 'listen' is parsed; ConfigParser::validateMandatoryFields
        // checks for that sentinel to detect a missing mandatory directive.
        ServerConfig()
            : listen_port(-1), host("0.0.0.0"), index("index.html"),
              client_max_body_size(1048576) {}

        void setListenPort(int port) { this->listen_port = port; }
        void setHost(const std::string &host) { this->host = host; }
        void setServerName(const std::string &name) { this->server_name = name; }
        void setRoot(const std::string &root) { this->root = root; }
        void setIndex(const std::string &index) { this->index = index; }
        void setClientMaxBodySize(long size) { this->client_max_body_size = size; }

        void addErrorPage(int code, const std::string &path)
        {
            ErrorPage ep;
            ep.code = code;
            ep.path = path;
            this->error_pages.push_back(ep);
        }

        void addLocation(const LocationBlock &loc) { this->locations.push_back(loc); }

        int                 getListenPort() const { return this->listen_port; }
        const std::string  &getHost() const { return this->host; }
        const std::string  &getServerName() const { return this->server_name; }
        const std::string  &getRoot() const { return this->root; }
        const std::string  &getIndex() const { return this->index; }
        long                getClientMaxBodySize() const { return this->client_max_body_size; }
        const std::vector<ErrorPage>     &getErrorPages() const { return this->error_pages; }
        const std::vector<LocationBlock> &getLocations() const { return this->locations; }

    private:
        int         listen_port;
        std::string host;
        std::string server_name;
        std::string root;
        std::string index;
        long        client_max_body_size;
        std::vector<ErrorPage>     error_pages;
        std::vector<LocationBlock> locations;
};

#endif
