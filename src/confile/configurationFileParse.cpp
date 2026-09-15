#include "configurationFileParse.hpp"
#include <cstdlib>
#include <sstream>

ConfigParseException::ConfigParseException(const std::string &message, int line)
{
    std::ostringstream oss;
    oss << "Line " << line << ": " << message;
    this->full_message = oss.str();
}

ConfigParseException::~ConfigParseException() throw() {}

const char *ConfigParseException::what() const throw()
{
    return this->full_message.c_str();
}

ConfigParser::ConfigParser() {}

ConfigParser::~ConfigParser() {}

const std::vector<ServerConfig> &ConfigParser::getServers() const { return this->servers_; }

const std::string &ConfigParser::getErrorMessage() const { return this->error_message_; }

// strtol + endptr check instead of atoi(), which silently returns 0 on
// garbage input (e.g. "listen abc;" would become port 0 with atoi()).
bool ConfigParser::parseIntStrict(const std::string &s, int &out)
{
    if (s.empty())
        return false;
    char *endptr;
    long value = std::strtol(s.c_str(), &endptr, 10);
    if (*endptr != '\0')
        return false;
    out = static_cast<int>(value);
    return true;
}

bool ConfigParser::parseLongStrict(const std::string &s, long &out)
{
    if (s.empty())
        return false;
    char *endptr;
    long value = std::strtol(s.c_str(), &endptr, 10);
    if (*endptr != '\0')
        return false;
    out = value;
    return true;
}

bool ConfigParser::parseSizeStrict(const std::string &s, long &out)
{
    if (s.empty())
        return false;
    char *endptr;
    long value = std::strtol(s.c_str(), &endptr, 10);
    if (value < 0)
        return false;
    
    // Handle size suffixes: M (megabytes), K (kilobytes), G (gigabytes)
    if (*endptr != '\0')
    {
        std::string suffix(endptr);
        if (suffix == "M")
            value *= 1024 * 1024;
        else if (suffix == "K")
            value *= 1024;
        else if (suffix == "G")
            value *= 1024 * 1024 * 1024;
        else
            return false; // Invalid suffix
    }
    out = value;
    return true;
}

// Whitelist check so a typo'd or unsupported directive is rejected with a
// clear error instead of being silently ignored (or, worse, silently doing
// nothing the way the old stub's tokenizer loop did).
bool ConfigParser::isKnownDirective(const std::string &word, DirectiveScope scope) const
{
    static const char *serverDirectives[] = {
        "listen", "host", "server_name", "error_page",
        "client_max_body_size", "client_read_timeout", "root", "index", 0
    };
    static const char *locationDirectives[] = {
        "root", "autoindex", "allow_methods", "index",
        "return", "alias", "cgi_path", "cgi_ext",
        "client_max_body_size", 0
    };

    const char **list = (scope == SCOPE_SERVER) ? serverDirectives : locationDirectives;
    for (int i = 0; list[i] != 0; i++)
    {
        if (word == list[i])
            return true;
    }
    return false;
}

// Hand-rolled char-by-char scan rather than line + stringstream: values are
// often glued directly to punctuation (e.g. "8001;", ".py .sh"), so splitting
// on whitespace alone would swallow "8001;" as a single word instead of the
// two tokens "8001" and ";". Scanning char-by-char lets {, }, ; be recognized
// as delimiters no matter what they're touching.
std::vector<ConfigParser::Token> ConfigParser::tokenize(std::ifstream &file)
{
    std::vector<Token> tokens;
    int line = 1;
    std::string word;
    bool inWord = false;
    char c;

    while (file.get(c))
    {
        if (c == '\n' || c == ' ' || c == '\t' || c == '\r' ||
            c == '{' || c == '}' || c == ';' || c == '#')
        {
            // Any delimiter first flushes whatever word was being accumulated.
            if (inWord)
            {
                Token t;
                t.type = Token::WORD;
                t.value = word;
                t.line = line;
                tokens.push_back(t);
                word.clear();
                inWord = false;
            }
            if (c == '{' || c == '}' || c == ';')
            {
                Token t;
                t.type = (c == '{') ? Token::LBRACE
                       : (c == '}') ? Token::RBRACE
                       : Token::SEMICOLON;
                t.value = std::string(1, c);
                t.line = line;
                tokens.push_back(t);
            }
            else if (c == '#')
            {
                // Comment: discard everything up to (and including) the next
                // newline, or EOF if the comment is on the last line.
                char cc;
                while (file.get(cc) && cc != '\n') { }
                if (!file.eof())
                    line++;
                continue;
            }
            if (c == '\n')
                line++;
            continue;
        }
        word += c;
        inWord = true;
    }
    if (inWord)
    {
        Token t;
        t.type = Token::WORD;
        t.value = word;
        t.line = line;
        tokens.push_back(t);
    }
    return tokens;
}

// Consumes every WORD token starting at pos (the directive's arguments) and
// the terminating ';', advancing pos past it. Shared by both
// parseServerDirective and parseLocationDirective since 'name value+ ;' is
// the shape of every directive regardless of scope.
std::vector<std::string> ConfigParser::collectValuesUntilSemicolon(size_t &pos, const std::string &directive)
{
    std::vector<std::string> values;
    while (pos < this->tokens_.size() && this->tokens_[pos].type == Token::WORD)
    {
        values.push_back(this->tokens_[pos].value);
        pos++;
    }
    if (pos >= this->tokens_.size())
        throw ConfigParseException("unexpected end of file inside directive '" + directive + "'",
                                    this->tokens_.empty() ? 0 : this->tokens_.back().line);
    if (this->tokens_[pos].type != Token::SEMICOLON)
        throw ConfigParseException("missing ';' after directive '" + directive + "'", this->tokens_[pos].line);
    pos++;
    return values;
}

void ConfigParser::parseServerDirective(size_t &pos, ServerConfig &sc, const std::string &directive, int directiveLine)
{
    std::vector<std::string> values = this->collectValuesUntilSemicolon(pos, directive);

    if (directive == "listen")
    {
        if (values.size() != 1)
            throw ConfigParseException("'listen' expects exactly one value", directiveLine);
        int port;
        if (!parseIntStrict(values[0], port) || port < 1 || port > 65535)
            throw ConfigParseException("invalid port value '" + values[0] + "' for 'listen'", directiveLine);
        sc.setListenPort(port);
    }
    else if (directive == "host")
    {
        if (values.size() != 1)
            throw ConfigParseException("'host' expects exactly one value", directiveLine);
        sc.setHost(values[0]);
    }
    else if (directive == "server_name")
    {
        if (values.empty())
            throw ConfigParseException("'server_name' expects at least one value", directiveLine);
        sc.setServerName(values[0]);
    }
    else if (directive == "root")
    {
        if (values.size() != 1)
            throw ConfigParseException("'root' expects exactly one value", directiveLine);
        sc.setRoot(values[0]);
    }
    else if (directive == "index")
    {
        if (values.size() != 1)
            throw ConfigParseException("'index' expects exactly one value", directiveLine);
        sc.setIndex(values[0]);
    }
    else if (directive == "client_max_body_size")
    {
        if (values.size() != 1)
            throw ConfigParseException("'client_max_body_size' expects exactly one value", directiveLine);
        long size;
        if (!parseSizeStrict(values[0], size) || size < 0)
            throw ConfigParseException("invalid size value '" + values[0] + "' for 'client_max_body_size'", directiveLine);
        sc.setClientMaxBodySize(size);
    }
    else if (directive == "client_read_timeout")
    {
        if (values.size() != 1)
            throw ConfigParseException("'client_read_timeout' expects exactly one value", directiveLine);
        long seconds;
        if (!parseLongStrict(values[0], seconds) || seconds < 0)
            throw ConfigParseException("invalid value '" + values[0] + "' for 'client_read_timeout'", directiveLine);
        sc.setClientReadTimeoutSeconds(seconds);
    }
    else if (directive == "error_page")
    {
        if (values.size() < 2)
            throw ConfigParseException("'error_page' expects one or more codes followed by a path", directiveLine);
        // Last value is the path; every value before it is a status code
        // sharing that same path (nginx's "error_page 404 500 /x.html;" form).
        std::string path = values[values.size() - 1];
        for (size_t i = 0; i + 1 < values.size(); i++)
        {
            int code;
            if (!parseIntStrict(values[i], code))
                throw ConfigParseException("invalid status code '" + values[i] + "' in 'error_page'", directiveLine);
            sc.addErrorPage(code, path);
        }
    }
}

void ConfigParser::parseLocationDirective(size_t &pos, LocationBlock &loc, const std::string &directive, int directiveLine)
{
    std::vector<std::string> values = this->collectValuesUntilSemicolon(pos, directive);

    if (directive == "root")
    {
        if (values.size() != 1)
            throw ConfigParseException("'root' expects exactly one value", directiveLine);
        loc.root = values[0];
    }
    else if (directive == "index")
    {
        if (values.size() != 1)
            throw ConfigParseException("'index' expects exactly one value", directiveLine);
        loc.index = values[0];
    }
    else if (directive == "return")
    {
        if (values.size() != 1)
            throw ConfigParseException("'return' expects exactly one value", directiveLine);
        loc.return_path = values[0];
    }
    else if (directive == "alias")
    {
        if (values.size() != 1)
            throw ConfigParseException("'alias' expects exactly one value", directiveLine);
        loc.alias = values[0];
    }
    else if (directive == "autoindex")
    {
        if (values.size() != 1 || (values[0] != "on" && values[0] != "off"))
            throw ConfigParseException("'autoindex' expects 'on' or 'off'", directiveLine);
        loc.autoindex = (values[0] == "on");
    }
    else if (directive == "allow_methods")
    {
        if (values.empty())
            throw ConfigParseException("'allow_methods' expects at least one value", directiveLine);
        loc.allow_methods = values;
    }
    else if (directive == "cgi_path")
    {
        if (values.empty())
            throw ConfigParseException("'cgi_path' expects at least one value", directiveLine);
        loc.cgi_path = values;
    }
    else if (directive == "cgi_ext")
    {
        if (values.empty())
            throw ConfigParseException("'cgi_ext' expects at least one value", directiveLine);
        loc.cgi_ext = values;
    }
    else if (directive == "client_max_body_size")
    {
        if (values.size() != 1)
            throw ConfigParseException("'client_max_body_size' expects exactly one value", directiveLine);
        long size;
        if (!parseSizeStrict(values[0], size) || size < 0)
            throw ConfigParseException("invalid size value '" + values[0] + "' for 'client_max_body_size'", directiveLine);
        loc.client_max_body_size = size;
    }
}

// Called with pos already past the 'location' keyword. Recursion (via the
// pos cursor being passed by reference through parseServerBlock) means each
// block consumes tokens up to its own matching '}' and returns, so nesting
// 'server { location { } }' needs no manual brace-depth counter.
LocationBlock ConfigParser::parseLocationBlock(size_t &pos)
{
    LocationBlock loc;

    if (pos >= this->tokens_.size() || this->tokens_[pos].type != Token::WORD)
        throw ConfigParseException("expected location path after 'location'",
                                    this->tokens_.empty() ? 0 : this->tokens_.back().line);
    loc.path = this->tokens_[pos].value;
    int pathLine = this->tokens_[pos].line;
    pos++;

    if (pos >= this->tokens_.size() || this->tokens_[pos].type != Token::LBRACE)
        throw ConfigParseException("expected '{' after location path '" + loc.path + "'", pathLine);
    int openLine = this->tokens_[pos].line;
    pos++;

    while (pos < this->tokens_.size() && this->tokens_[pos].type != Token::RBRACE)
    {
        if (this->tokens_[pos].type != Token::WORD)
            throw ConfigParseException("expected directive name inside location block", this->tokens_[pos].line);
        std::string directive = this->tokens_[pos].value;
        int directiveLine = this->tokens_[pos].line;
        if (directive == "location")
            throw ConfigParseException("nested 'location' blocks are not supported", directiveLine);
        if (!this->isKnownDirective(directive, SCOPE_LOCATION))
            throw ConfigParseException("unknown directive '" + directive + "'", directiveLine);
        pos++;
        this->parseLocationDirective(pos, loc, directive, directiveLine);
    }
    if (pos >= this->tokens_.size())
        throw ConfigParseException("unexpected end of file: unmatched '{' opened", openLine);
    pos++;
    return loc;
}

// Called with pos pointing at the 'server' keyword itself.
ServerConfig ConfigParser::parseServerBlock(size_t &pos)
{
    ServerConfig sc;
    int keywordLine = this->tokens_[pos].line;
    pos++;

    if (pos >= this->tokens_.size() || this->tokens_[pos].type != Token::LBRACE)
        throw ConfigParseException("expected '{' after 'server'", keywordLine);
    int openLine = this->tokens_[pos].line;
    pos++;

    while (pos < this->tokens_.size() && this->tokens_[pos].type != Token::RBRACE)
    {
        if (this->tokens_[pos].type != Token::WORD)
            throw ConfigParseException("expected directive name inside server block", this->tokens_[pos].line);
        std::string directive = this->tokens_[pos].value;
        int directiveLine = this->tokens_[pos].line;
        if (directive == "location")
        {
            pos++;
            sc.addLocation(this->parseLocationBlock(pos));
        }
        else
        {
            if (!this->isKnownDirective(directive, SCOPE_SERVER))
                throw ConfigParseException("unknown directive '" + directive + "'", directiveLine);
            pos++;
            this->parseServerDirective(pos, sc, directive, directiveLine);
        }
    }
    if (pos >= this->tokens_.size())
        throw ConfigParseException("unexpected end of file: unmatched '{' opened", openLine);
    pos++;
    return sc;
}

void ConfigParser::validateMandatoryFields(const ServerConfig &sc, int serverLine) const
{
    if (sc.getListenPort() == -1)
        throw ConfigParseException("server block missing mandatory 'listen' directive", serverLine);
    if (sc.getRoot().empty())
        throw ConfigParseException("server block missing mandatory 'root' directive", serverLine);
}

// Top-level grammar: the whole file is just one or more 'server { ... }'
// blocks back to back (supports multi-server/virtual-host configs later
// without any change to this loop).
void ConfigParser::parseTokens()
{
    size_t pos = 0;
    if (this->tokens_.empty())
        throw ConfigParseException("empty configuration file", 0);
    while (pos < this->tokens_.size())
    {
        if (this->tokens_[pos].type != Token::WORD || this->tokens_[pos].value != "server")
            throw ConfigParseException("expected 'server' block at top level", this->tokens_[pos].line);
        int serverLine = this->tokens_[pos].line;
        ServerConfig sc = this->parseServerBlock(pos);
        this->validateMandatoryFields(sc, serverLine);
        this->servers_.push_back(sc);
    }
}

// The only public entry point that touches parsing. It is the exception
// boundary: everything below (tokenize/parseTokens/...) throws
// ConfigParseException on any malformed input, but that never escapes past
// here — callers only ever see a bool + getErrorMessage(), matching the rest
// of the codebase's error-handling style and guaranteeing a bad config file
// can't crash the process.
bool ConfigParser::parseFile(const std::string &filename)
{
    std::ifstream file(filename.c_str());
    if (!file.is_open())
    {
        this->error_message_ = "cannot open configuration file '" + filename + "'";
        return false;
    }
    try
    {
        this->tokens_ = this->tokenize(file);
        file.close();
        this->parseTokens();
    }
    catch (const ConfigParseException &e)
    {
        this->error_message_ = e.what();
        return false;
    }
    return true;
}
