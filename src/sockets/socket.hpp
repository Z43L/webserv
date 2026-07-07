#ifndef SOCKET_HPP
#define SOCKET_HPP
#include "../server.hpp"

template <typename T>
class Socket : public server{
    private:
        
    public:
    // crear parseo luego en otra clase de las peticiones GET
    // PUT POST DELETE OPTIONS
        std::map<std::string T> parse_input(std::map <std::string T> requestParser, parseInputRequest& request )  

};

#endif