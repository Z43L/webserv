#include "../sockets-includes/parseInputRequest.hpp"

ParseInputRequest::ParseInputRequest() : method(static_cast<t_method>(0)) {
  type = "";
  url = "";
  dns = "";
  header = "";
  body = "";
  content = "";
  securitiHeader = "";
}
ParseInputRequest::~ParseInputRequest() {}

const std::string &ParseInputRequest::getType() const { return type; }

const std::string &ParseInputRequest::getUrl() const { return url; }

const std::string &ParseInputRequest::getDns() const { return dns; }

const std::string &ParseInputRequest::getHeader() const { return header; }

const std::string &ParseInputRequest::getBody() const { return body; }

const std::string &ParseInputRequest::getContent() const { return content; }

const std::string &ParseInputRequest::getSecurityHeader() const {
  return securitiHeader;
}

void ParseInputRequest::parse(const std::string &rawInput) {
  if (rawInput.empty()) {
    return;
  }

  size_t reqLineEnd = rawInput.find("\r\n");
  if (reqLineEnd == std::string::npos) {
    return;
  }
  std::string reqLine = rawInput.substr(0, reqLineEnd);

  size_t firstSpace = reqLine.find(' ');
  if (firstSpace == std::string::npos) {
    return;
  }

  std::string methodStr = reqLine.substr(0, firstSpace);

  size_t secondSpace = reqLine.find(' ', firstSpace + 1);
  if (secondSpace == std::string::npos) {

    url = reqLine.substr(firstSpace + 1);
  } else {
    url = reqLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
  }

  if (methodStr == "GET") {
    method = GET;
  } else if (methodStr == "POST") {
    method = POST;
  } else if (methodStr == "PUT") {
    method = PUT;
  } else if (methodStr == "DELETE") {
    method = DELETE;
  } else if (methodStr == "OPTIONS") {
    method = OPTIONS;
  } else {
    method = static_cast<t_method>(0); // Método desconocido/no soportado
  }

  size_t headerStart = reqLineEnd + 2;
  size_t doubleCRLF = rawInput.find("\r\n\r\n");

  if (doubleCRLF == std::string::npos) {

    doubleCRLF = rawInput.size();
  }

  if (headerStart < doubleCRLF) {
    header = rawInput.substr(headerStart, doubleCRLF - headerStart);

    size_t pos = 0;
    size_t totalLen = header.length();

    while (pos < totalLen) {
      size_t nextLine = header.find("\r\n", pos);
      std::string line;

      if (nextLine == std::string::npos) {
        line = header.substr(pos);
        pos = totalLen;
      } else {
        line = header.substr(pos, nextLine - pos);
        pos = nextLine + 2;
      }

      if (line.empty()) {
        continue;
      }

      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);

        while (!key.empty() && (key == " " || key == "\t")) {
          key.erase(0, 1);
        }
        while (!key.empty() && (key[key.length() - 1] == ' ' ||
                                key[key.length() - 1] == '\t')) {
          key.erase(key.length() - 1, 1);
        }
        while (!val.empty() && (val == " " || val == "\t")) {
          val.erase(0, 1);
        }
        while (!val.empty() && (val[val.length() - 1] == ' ' ||
                                val[val.length() - 1] == '\t')) {
          val.erase(val.length() - 1, 1);
        }

        std::string lowerKey = key;
        for (size_t i = 0; i < lowerKey.length(); ++i) {
          if (lowerKey[i] >= 'A' && lowerKey[i] <= 'Z') {
            lowerKey[i] = lowerKey[i] - 'A' + 'a';
          }
        }

        if (lowerKey == "host") {
          dns = val;
        } else if (lowerKey == "content-type") {
          type = val;
        } else if (lowerKey == "authorization" ||
                   lowerKey == "x-security-token") {
          securitiHeader = val;
        }
      }
    }
  }

  size_t bodyStart = rawInput.find("\r\n\r\n");
  if (bodyStart != std::string::npos && bodyStart + 4 < rawInput.size()) {
    body = rawInput.substr(bodyStart + 4);
    content = body;
  }
}
bool ParseInputRequest::is_request_complete(const std::string &rawInput) {

  size_t headers_end = rawInput.find("\r\n\r\n");
  if (headers_end == std::string::npos) {
    return false;
  }

  std::string headers = rawInput.substr(0, headers_end);

  size_t cl_pos = headers.find("Content-Length:");
  if (cl_pos == std::string::npos) {
    cl_pos = headers.find("content-length:");
  }

  if (cl_pos != std::string::npos) {
    size_t line_end = headers.find("\r\n", cl_pos);
    std::string cl_line = headers.substr(cl_pos, line_end - cl_pos);

    size_t colon_pos = cl_line.find(":");
    std::string value_str = cl_line.substr(colon_pos + 1);

    while (!value_str.empty() && (value_str == " " || value_str == "\t")) {
      value_str.erase(0, 1);
    }

    long content_length = std::atol(value_str.c_str());

    size_t body_bytes_received = rawInput.size() - (headers_end + 4);

    return (body_bytes_received >= static_cast<size_t>(content_length));
  }

  return true;
}
