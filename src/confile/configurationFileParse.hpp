#ifndef CONFIGURATIONFILEPARSE_HPP
#define CONFIGURATIONFILEPARSE_HPP
#include <string>
#include <cstdlib>
#include <vector>

class   ConfigurationFileParse
{
    private:
        std::string listen_port;
        std::string host;
        //std::string server_name;
        std::string error_page;
        std::string root_page;
        std::string index;
        int client_max_body_size;
        std::string cgi_path;
        std::vector<std::string> cgi_extensions;

    public:
        ConfigurationFileParse();
        void    setListenPort(std::string listen_port);
        void    setHost(std::string host);
        void    setErrorPage(std::string error_page);
        void    setRootPage(std::string root_page);
        void    setIndex(std::string index);
        ~ConfigurationFileParse();

    private: //routes
        std::string routes_root;
        std::string routes_index;
        bool    routes_autoindex;
        //std::vector<methods> allow_methods;
        std::string routes_redirection;
        std::string routes_alias;

    public:
        void    parseConfigurationFile();
};

#endif
