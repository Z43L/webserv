#include "../cgi-includes/parserCgi.hpp"

Parsercgi::Parsercgi(int fd) : wrrong(false), fd(fd), isFin(NULL) {}

int Parsercgi::findCgi(std::string bufferRead, std::string key) {
  size_t pos = bufferRead.find(key);
  if (pos == std::string::npos) {
    return -1;
  }
  return static_cast<int>(pos);
}

std::string Parsercgi::BufferRead(ssize_t fd) {
  char temp_buff[BUFFER_SIZE];
  std::string result;

  ssize_t bytes_read = read(fd, temp_buff, BUFFER_SIZE - 1);
  if (bytes_read > 0) {
    temp_buff[bytes_read] = '\0';
    result = temp_buff;
  } else if (bytes_read < 0) {
    std::clog << "Error al intentar leer del fd: " << fd << std::endl;
  }
  return result;
}

std::map<std::string, std::string> Parsercgi::ReadMapper(int calculatorPosition,
                                                         int bufferReadLength) {
  std::map<std::string, std::string> mapper;

  int file_fd = open(this->filePath.c_str(), O_RDONLY);
  if (file_fd < 0) {
    std::cerr << "error al leer el archivo CGI" << std::endl;
    this->wrrong = true;
    return mapper;
  }

  char buff[BUFFER_SIZE];
  ssize_t lread = read(file_fd, buff, BUFFER_SIZE - 1);
  if (lread < 0) {
    std::cerr << "error al intentar leer" << std::endl;
    close(file_fd);
    return mapper;
  }

  std::string accumulator = "";
  while (lread > 0) {
    buff[lread] = '\0';
    accumulator += buff;
    lread = read(file_fd, buff, BUFFER_SIZE - 1);
  }
  close(file_fd);

  size_t pos = 0;
  while (pos < accumulator.length()) {
    size_t next_line = accumulator.find('\n', pos);
    std::string line = (next_line == std::string::npos)
                           ? accumulator.substr(pos)
                           : accumulator.substr(pos, next_line - pos);

    if (!line.empty()) {
      size_t space_pos = line.find(' ');
      if (space_pos != std::string::npos) {
        std::string key = line.substr(0, space_pos);
        std::string value = line.substr(space_pos + 1);

        mapper[key] = value;
        this->readMapper.push_back(key);
        this->readMapper.push_back(value);
      }
    }

    if (next_line == std::string::npos)
      break;
    pos = next_line + 1;
  }

  return mapper;
}

int Parsercgi::finCgi(std::string bufferRead) {
  return findCgi(bufferRead, "\r\n\r\n");
}
