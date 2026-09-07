#include "parseInputRequest.hpp"

ParseInputRequest::ParseInputRequest() : method(static_cast<t_method>(0)) {
  methodStr = "";
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
const std::string &ParseInputRequest::getMethod() const { return methodStr; }
long ParseInputRequest::getContentLength() const {
  const std::string &h = rawInput;
  size_t p = h.find("Content-Length:");
  if (p == std::string::npos)
    p = h.find("content-length:");
  if (p == std::string::npos)
    return 0;
  size_t e = h.find("\r\n", p);
  if (e == std::string::npos)
    e = h.size();
  std::string v = h.substr(p, e - p);
  size_t c = v.find(':');
  v = v.substr(c + 1);
  while (!v.empty() && (v[0] == ' ' || v[0] == '\t'))
    v.erase(0, 1);
  return std::atol(v.c_str());
}

const std::string &ParseInputRequest::getUrl() const { return url; }

const std::string &ParseInputRequest::getDns() const { return dns; }

const std::string &ParseInputRequest::getHeader() const { return header; }

const std::string &ParseInputRequest::getBody() const { return body; }

const std::string &ParseInputRequest::getContent() const { return content; }

const std::string &ParseInputRequest::getSecurityHeader() const {
  return securitiHeader;
}

void ParseInputRequest::parse(const std::string &rawInput) {
  this->rawInput = rawInput;
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

  this->methodStr = methodStr;
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

  // Chunked transfer encoding: complete when the terminating 0-length
  // chunk ("0\r\n\r\n" or "0\r\n<trailers>\r\n") has been received.
  size_t te_pos = headers.find("Transfer-Encoding:");
  if (te_pos == std::string::npos) te_pos = headers.find("transfer-encoding:");
  if (te_pos != std::string::npos) {
    size_t te_end = headers.find("\r\n", te_pos);
    std::string te_val = headers.substr(te_pos, te_end - te_pos);
    for (size_t i = 0; i < te_val.size(); ++i)
      te_val[i] = static_cast<char>(std::tolower(te_val[i]));
    if (te_val.find("chunked") != std::string::npos) {
      size_t body_start = headers_end + 4;
      // Walk chunks; if any still pending or malformed, not complete.
      size_t pos = body_start;
      while (pos < rawInput.size()) {
        size_t eol = rawInput.find("\r\n", pos);
        if (eol == std::string::npos) return false;
        std::string size_line = rawInput.substr(pos, eol - pos);
        size_t sc = size_line.find(';');
        if (sc != std::string::npos) size_line = size_line.substr(0, sc);
        char *endptr;
        long chunk_size = std::strtol(size_line.c_str(), &endptr, 16);
        if (*endptr != '\0' && *endptr != ' ' && *endptr != '\t') return false;
        if (chunk_size < 0) return false;
        if (chunk_size == 0) {
          // Terminator: skip optional trailers up to the empty line.
          size_t trailer = eol + 2;
          while (trailer < rawInput.size()) {
            size_t tend = rawInput.find("\r\n", trailer);
            if (tend == std::string::npos) return false;
            if (tend == trailer) return true;
            trailer = tend + 2;
          }
          return false;
        }
        size_t data_pos = eol + 2;
        if (data_pos + static_cast<size_t>(chunk_size) + 2 > rawInput.size()) return false;
        pos = data_pos + static_cast<size_t>(chunk_size) + 2;
      }
      return false;
    }
  }

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

long ParseInputRequest::getContentLengthFromRaw(const std::string &raw) {
  size_t headers_end = raw.find("\r\n\r\n");
  if (headers_end == std::string::npos)
    headers_end = raw.size();
  std::string headers = raw.substr(0, headers_end);
  size_t p = headers.find("Content-Length:");
  if (p == std::string::npos)
    p = headers.find("content-length:");
  if (p == std::string::npos)
    return 0;
  size_t e = headers.find("\r\n", p);
  if (e == std::string::npos) e = headers.size();
  std::string v = headers.substr(p, e - p);
  size_t c = v.find(':');
  v = v.substr(c + 1);
  while (!v.empty() && (v[0] == ' ' || v[0] == '\t'))
    v.erase(0, 1);
  return std::atol(v.c_str());
}

bool ParseInputRequest::isChunked(const std::string &raw) {
  size_t headers_end = raw.find("\r\n\r\n");
  if (headers_end == std::string::npos) headers_end = raw.size();
  std::string headers = raw.substr(0, headers_end);
  size_t p = headers.find("Transfer-Encoding:");
  if (p == std::string::npos) p = headers.find("transfer-encoding:");
  if (p == std::string::npos) return false;
  size_t e = headers.find("\r\n", p);
  if (e == std::string::npos) e = headers.size();
  std::string v = headers.substr(p, e - p);
  size_t c = v.find(':');
  v = v.substr(c + 1);
  while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v.erase(0, 1);
  for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<char>(std::tolower(v[i]));
  return v.find("chunked") != std::string::npos;
}

size_t ParseInputRequest::decodedChunkedSize(const std::string &raw) {
  size_t headers_end = raw.find("\r\n\r\n");
  if (headers_end == std::string::npos) return 0;
  size_t pos = headers_end + 4;
  size_t total = 0;
  while (pos < raw.size()) {
    size_t eol = raw.find("\r\n", pos);
    if (eol == std::string::npos) break;
    std::string size_line = raw.substr(pos, eol - pos);
    size_t sc = size_line.find(';');
    if (sc != std::string::npos) size_line = size_line.substr(0, sc);
    char *endptr;
    long chunk_size = std::strtol(size_line.c_str(), &endptr, 16);
    if (*endptr != '\0' && *endptr != ' ' && *endptr != '\t') break;
    if (chunk_size < 0) break;
    if (chunk_size == 0) return total;
    total += static_cast<size_t>(chunk_size);
    pos = eol + 2 + chunk_size + 2;
  }
  return total;
}

std::string ParseInputRequest::decodeChunked(const std::string &raw) {
  std::string out;
  size_t headers_end = raw.find("\r\n\r\n");
  if (headers_end == std::string::npos) return out;
  size_t pos = headers_end + 4;
  while (pos < raw.size()) {
    size_t eol = raw.find("\r\n", pos);
    if (eol == std::string::npos) break;
    std::string size_line = raw.substr(pos, eol - pos);
    size_t sc = size_line.find(';');
    if (sc != std::string::npos) size_line = size_line.substr(0, sc);
    char *endptr;
    long chunk_size = std::strtol(size_line.c_str(), &endptr, 16);
    if (*endptr != '\0' && *endptr != ' ' && *endptr != '\t') break;
    if (chunk_size < 0) break;
    if (chunk_size == 0) {
      // Optional trailers (key:value\r\n) followed by \r\n; skip until done.
      size_t trailer = eol + 2;
      while (trailer < raw.size()) {
        size_t tend = raw.find("\r\n", trailer);
        if (tend == std::string::npos || tend == trailer) break;
        trailer = tend + 2;
      }
      break;
    }
    size_t data_pos = eol + 2;
    if (data_pos + static_cast<size_t>(chunk_size) > raw.size()) break;
    out.append(raw, data_pos, static_cast<size_t>(chunk_size));
    pos = data_pos + static_cast<size_t>(chunk_size) + 2;
  }
  return out;
}
