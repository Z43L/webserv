#ifndef PARSEINPUTREQUEST_HPP
#define PARSEINPUTREQUEST_HPP

#include <cstdlib>
#include <iostream>
#include <string>
// template <typename T>
class ParseInputRequest {
  typedef enum e_method {
    GET = 7171,
    POST = 7272,
    PUT = 7373,
    DELETE = 7474,
    OPTIONS = 7575
  } t_method;

private:
  std::string rawInput;
  t_method method;
  std::string methodStr;
  std::string type;
  std::string url;
  std::string dns;
  std::string header;
  std::string body;
  std::string content;
  std::string securitiHeader;

public:
  ParseInputRequest();
  ~ParseInputRequest();
  bool is_request_complete(const std::string &rawInput);
  void parse(const std::string &rawInput);
  const std::string &getType() const;
  const std::string &getMethod() const;
  const std::string &getUrl() const;
  long                getContentLength() const;
  static long         getContentLengthFromRaw(const std::string &raw);
  static bool         isChunked(const std::string &raw);
  static size_t       decodedChunkedSize(const std::string &raw);
  static std::string  decodeChunked(const std::string &raw);
  const std::string &getDns() const;
  const std::string &getHeader() const;
  const std::string &getBody() const;
  const std::string &getContent() const;
  const std::string &getSecurityHeader() const;
};

#endif
