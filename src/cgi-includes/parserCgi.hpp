#ifndef PARSERCGI_HPP
#define PARSERCGI_HPP

#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <stdio.h>
#include <unistd.h>

#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>
#define BUFFER_SIZE 4096
class Parsercgi {
private:
  std::vector<std::string> readMapper;
  bool wrrong;
  std::string filePath;
  std::string bufferRead;
  int fd;
  char *isFin;

public:
  Parsercgi() {};
  Parsercgi(int fd);
  ~Parsercgi() {};
  int calculatorPosition(std::string, size_t lenBufferRead);
  std::string BufferRead(ssize_t fd);
  std::map<std::string, std::string> ReadMapper(int calculatorPosition,
                                                int bufferRead);
  int findCgi(std::string bufferRead, std::string key);
  int finCgi(std::string bufferRead);
};

#endif
