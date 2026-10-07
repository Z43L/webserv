#ifndef CONFIGURATIONFILEPARSE_HPP
#define CONFIGURATIONFILEPARSE_HPP

#include <string>
#include <vector>
#include <fstream>
#include <exception>
#include "serverConfig.hpp"

class ConfigParseException : public std::exception {
    public:
        ConfigParseException(const std::string &message, int line);
        virtual ~ConfigParseException() throw();
        virtual const char *what() const throw();

    private:
        std::string full_message;
};

class ConfigParser {
    public:
        ConfigParser();
        ~ConfigParser();

        bool parseFile(const std::string &filename);
        const std::vector<ServerConfig> &getServers() const;
        const std::string &getErrorMessage() const;

    private:
        enum DirectiveScope { SCOPE_SERVER, SCOPE_LOCATION };

        struct Token {
            enum Type { WORD, LBRACE, RBRACE, SEMICOLON } type;
            std::string value;
            int line;
        };

        std::vector<Token>        tokens_;
        std::vector<ServerConfig> servers_;
        std::string               error_message_;

        std::vector<Token> tokenize(std::ifstream &file);
        void         parseTokens();
        ServerConfig parseServerBlock(size_t &pos);
        LocationBlock parseLocationBlock(size_t &pos);
        void parseServerDirective(size_t &pos, ServerConfig &sc, const std::string &directive, int directiveLine);
        void parseLocationDirective(size_t &pos, LocationBlock &loc, const std::string &directive, int directiveLine);
        std::vector<std::string> collectValuesUntilSemicolon(size_t &pos, const std::string &directive);
        void validateMandatoryFields(const ServerConfig &sc, int serverLine) const;
        bool isKnownDirective(const std::string &word, DirectiveScope scope) const;

        static bool parseIntStrict(const std::string &s, int &out);
        static bool parseLongStrict(const std::string &s, long &out);
        static bool parseSizeStrict(const std::string &s, long &out);

        ConfigParser(const ConfigParser &other);
        ConfigParser &operator=(const ConfigParser &other);
};

#endif
