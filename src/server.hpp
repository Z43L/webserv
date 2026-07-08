#ifndef SERVER_HPP
#define SERVER_HPP
#pragma once

#include <string>
#include <map>
#include "sockets/parseInputRequest.hpp"
#include "sockets/request.hpp"
#include "sockets/securiti.hpp"
#include "sockets/socket.hpp"

template <typename T>
class server{
    private:
        std::string cgiFile;
        std::string cgiPath;
        std::map<int, std::string> _client_buffers;
        std::map<std::string T> responseParser;
        std::map<std::string T> requestParser;
        std::map<std::string T > cgiParser; 
    public:
        int server_init(std::map<std::string T> cgiParser);
        int parseRequest(std::sting cgiFile);
        std::string whatCgiFile;
        std::string whatCgiPath;
        std::map<std::string T> whatresponseParser;
        std::map<std::string T> whatrequestParser;
        std::map<std::string T > whatcgiParser;
    };


#endif