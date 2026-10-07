#ifndef PARSE_RESPONSE_HPP
#define PARSE_RESPONSE_HPP

#include <map>
#include <string>

class ParseResponse {
public:
  ParseResponse();
  ~ParseResponse();

  void setStatus(int code, const std::string &message);
  void setBody(const std::string &body);
  void setBodyFromFile(const std::string &filePath);
  void setHeader(const std::string &key, const std::string &value);

  std::string build() const;

  static std::string getMimeType(const std::string &filePath);
  static bool fileExists(const std::string &filePath);

private:
  int _statusCode;
  std::string _statusMessage;
  std::map<std::string, std::string> _headers;
  std::string _body;
};

#endif
