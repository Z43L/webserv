#include "parseResponse.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

ParseResponse::ParseResponse() : _statusCode(200), _statusMessage("OK") {}

ParseResponse::~ParseResponse() {}

void ParseResponse::setStatus(int code, const std::string &message) {
  _statusCode = code;
  _statusMessage = message;
}

void ParseResponse::setBody(const std::string &body) { _body = body; }

bool ParseResponse::fileExists(const std::string &filePath) {
  struct stat buffer;
  return (stat(filePath.c_str(), &buffer) == 0);
}

std::string ParseResponse::getMimeType(const std::string &filePath) {
  size_t dot = filePath.rfind('.');
  if (dot == std::string::npos)
    return "application/octet-stream";

  std::string ext = filePath.substr(dot + 1);

  if (ext == "html" || ext == "htm")
    return "text/html";
  if (ext == "css")
    return "text/css";
  if (ext == "js")
    return "application/javascript";
  if (ext == "png")
    return "image/png";
  if (ext == "jpg" || ext == "jpeg")
    return "image/jpeg";
  if (ext == "gif")
    return "image/gif";
  if (ext == "ico")
    return "image/x-icon";
  if (ext == "svg")
    return "image/svg+xml";
  if (ext == "json")
    return "application/json";
  if (ext == "pdf")
    return "application/pdf";
  if (ext == "txt")
    return "text/plain";

  return "application/octet-stream";
}

void ParseResponse::setBodyFromFile(const std::string &filePath) {
  std::ifstream file(filePath.c_str(), std::ios::binary);
  if (!file.is_open()) {
    _statusCode = 404;
    _statusMessage = "Not Found";
    _body = "<html><body><h1>404 Not Found</h1></body></html>";
    return;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  _body = buffer.str();

  setHeader("Content-Type", getMimeType(filePath));
}

void ParseResponse::setHeader(const std::string &key,
                              const std::string &value) {
  _headers[key] = value;
}

std::string ParseResponse::build() const {
  std::ostringstream response;

  response << "HTTP/1.1 " << _statusCode << " " << _statusMessage << "\r\n";

  std::map<std::string, std::string>::const_iterator it;
  bool hasContentLength = false;
  for (it = _headers.begin(); it != _headers.end(); ++it) {
    if (it->first == "Content-Length") {
      hasContentLength = true;
      continue;
    }
    response << it->first << ": " << it->second << "\r\n";
  }

  if (hasContentLength) {
    std::map<std::string, std::string>::const_iterator cl = _headers.find("Content-Length");
    response << "Content-Length: " << cl->second << "\r\n";
  } else {
    response << "Content-Length: " << _body.length() << "\r\n";
  }

  response << "Connection: close\r\n";

  response << "\r\n";
  response << _body;

  return response.str();
}
