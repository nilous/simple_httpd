#include <array>
#include <iostream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <asio.hpp>
#include <http_parser.h>

using tcp = asio::ip::tcp;

enum HTTPMethod {
  kDelete = 0,
  kGet,
  kHead,
  kPost,
  kPut,
  kConnect,
  kOptions,
  kTrace
};

struct Request {
  HTTPMethod method;
  std::unordered_map<std::string, std::string> headers;
  std::string body;
};

struct Response {
  //int status;  // fixme:
  std::unordered_map<std::string, std::string> headers;
  std::string body;

  std::string ToString() const {
    std::stringstream res;
    res << "HTTP/1.0 200 OK\r\n";
    for (const auto &h : headers)
      res << h.first << ":" << h.second << "\r\n";

    res << "Content-Length:" << std::to_string(body.size()) << "\r\n"
        << "\r\n"
        << body;
    return res.str();
  }
};

template <typename HANDLER>
struct RequestParser : public http_parser {
  HANDLER *handler;

  std::unordered_map<std::string, std::string> headers;
  std::string body;
  bool completed = false;

  std::string header_field;

  RequestParser() {
    http_parser_init(this, HTTP_REQUEST);
  }

  void Reset() {
    completed = false;
    headers.clear();
    body.clear();
  }

  static int OnMessageBegin(http_parser *parser) {
    auto self = static_cast<RequestParser *>(parser);
    //std::cout << self->method << '\n';
    return 0;
  }
  static int OnUrl(http_parser *parser, const char *at, size_t length) {
    return 0;
  }
  static int OnHeaderField(http_parser *parser, const char *at, size_t length) {
    auto self = static_cast<RequestParser *>(parser);
    self->header_field = std::string(at, length);
    return 0;
  }
  static int OnHeaderValue(http_parser *parser, const char *at, size_t length) {
    auto self = static_cast<RequestParser *>(parser);
    self->headers.emplace(std::move(self->header_field), std::string(at, length));
    return 0;
  }
  static int OnHeadersComplete(http_parser *parser) {
    return 0;
  }
  static int OnBody(http_parser *parser, const char *at, size_t length) {
    return 0;
  }
  static int OnMessageComplete(http_parser *parser) {
    auto self = static_cast<RequestParser *>(parser);
    self->completed = true;
    return 0;
  }

  Request ToRequest() const {
    return Request{ (HTTPMethod)method, std::move(headers), std::move(body) };
  }

  Response Handle() {
    return handler->Handle(ToRequest());
  }

  bool Feed(const char *data, size_t length) {
    http_parser_settings settings;
    settings.on_message_begin = OnMessageBegin;
    settings.on_url = OnUrl;
    settings.on_status = nullptr;
    settings.on_header_field = OnHeaderField;
    settings.on_header_value = OnHeaderValue;
    settings.on_headers_complete = OnHeadersComplete;
    settings.on_body = OnBody;
    settings.on_message_complete = OnMessageComplete;

    return http_parser_execute(this, &settings, data, length) == length;
  }
};

class NotCopyable {
public:
  NotCopyable() {}
  NotCopyable(const NotCopyable &) = delete;
  NotCopyable &operator=(const NotCopyable &) = delete;
};

class Connection : NotCopyable, public std::enable_shared_from_this<Connection> {
public:
  typedef std::shared_ptr<Connection> Ptr;

  static Ptr Create(asio::io_service &io) {
    return Ptr(new Connection(io));
  }

  void Start() { DoRead(); }

  tcp::socket &socket() { return socket_; }

  Response Handle(Request req) {
    Response response;
    switch (req.method) {
    case kGet:
      response.body = "Welcome to my HTTP server!";
      break;
    case kPost:
      response.body = "Your message body: ";
      response.body.insert(response.body.end(), req.body.begin(), req.body.end());
      break;
    default:
      // todo: support more method
      break;
    }

    return response;
  }

private:
  explicit Connection(asio::io_service &io)
    : socket_(io) {
  }

  void DoRead() {
    auto self = shared_from_this();
    socket_.async_read_some(asio::buffer(buffer_), [this, self] (const asio::error_code &ec, size_t bytes_transferred) {
      parser_.Feed(buffer_.data(), bytes_transferred);

      if (!ec) {
        if (parser_.Feed(buffer_.data(), bytes_transferred)) {
          if (parser_.completed) {
            auto res = parser_.Handle();
            socket_.write_some(asio::buffer(res.ToString()));
          } else {
            DoRead();
          }
        } else {
          // todo: bad req, response
        }

      }
    });
  }

  tcp::socket socket_;
  std::array<char, 8096> buffer_;
  RequestParser<Connection> parser_;
};

class Server : NotCopyable {
public:
  explicit Server(uint16_t port)
    : io_(),
      acceptor_(io_, tcp::endpoint(tcp::v4(), port)) {
    DoAccept();
  }

  void Run() { io_.run(); }

private:
  void DoAccept() {
    auto new_connection = Connection::Create(io_);

    acceptor_.async_accept(new_connection->socket(), [this, new_connection] (const asio::error_code &ec) {
      new_connection->Start();
      DoAccept();
    });
  }

  asio::io_service io_;
  tcp::acceptor acceptor_;
};


int main(int argc, char **argv) {
  Server s(4000);
  s.Run();

  system("PAUSE");
  return 0;
}