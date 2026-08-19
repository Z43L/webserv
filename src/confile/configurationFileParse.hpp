#ifndef CONFIGURATIONFILEPARSE_HPP
#define CONFIGURATIONFILEPARSE_HPP
#include <sstream>
#include <string>
#include <fstream>
#include <cstdlib>
#include <vector>

class   ConfigurationFileParse
{
    private:
        std::string filename;
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
        ConfigurationFileParse(char *filename);
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

    private:
        bool    is_eof;
        std::ifstream   file;
        std::string     line;
        std::stringstream   ss;
    public:
        bool    isAGoodParameter(std::string word);
        void    parseConfigurationFile();
};

#endif
