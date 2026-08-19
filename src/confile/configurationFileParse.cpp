#include "configurationFileParse.hpp"
#include <cstddef>
#include <iostream>
#include <ostream>
#include <string>

ConfigurationFileParse::ConfigurationFileParse(char *filename)
{
    this->filename = filename;
    this->error_page = "404.html";
    this->index = "index.html";
    this->client_max_body_size = 1024;
    this->is_eof = false;
}

bool    ConfigurationFileParse::isAGoodParameter(std::string word)
{
}

void    ConfigurationFileParse::parseConfigurationFile()
{
    this->file.open(this->filename.c_str());
    if (!this->file.is_open())
    {
        std::cerr << "Cant open configuration file" << std::endl;
        return;
    }
    while (std::getline(this->file, this->line))
    {
        std::cout << this->line << std::endl;
        ss.str(this->line);
        std::string word;
        while (ss >> word) {

        }
    }
    this->file.close();
}

ConfigurationFileParse::~ConfigurationFileParse()
{
    return ;
}
