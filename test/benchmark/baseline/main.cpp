#include <functional>
#include <iostream>
#include <cstdlib>

#define ASIO_STANDALONE
#include <asio.hpp>

using tcp = asio::ip::tcp;

asio::io_service g_io_service;
std::string g_listen_port("8000");
std::string g_target_port("8080");
std::string g_target_addr("127.0.0.1");

//
// Buffer
//

struct Buffer {
  Buffer* next = nullptr;
  size_t size = 0;
  char data[16*1024];

  static auto alloc() -> Buffer* {
    if (auto p = s_pool) {
      s_pool = p->next;
      return p;
    } else {
      return new Buffer;
    }
  }

  static void free(Buffer *buf) {
    buf->next = s_pool;
    s_pool = buf;
  }

private:
  static Buffer* s_pool;
};

Buffer* Buffer::s_pool = nullptr;

//
// Queue
//

struct Queue {
  Buffer* head = nullptr;
  Buffer* tail = nullptr;

  void push(Buffer *buf) {
    if (auto p = tail) {
      p->next = buf;
    } else {
      head = buf;
    }
    tail = buf;
    buf->next = nullptr;
  }

  auto shift() -> Buffer* {
    if (auto b = head) {
      head = b->next;
      if (!head) tail = nullptr;
      return b;
    } else {
      return nullptr;
    }
  }
};

//
// Session
//

struct Session {
  tcp::socket socket_d, socket_u;
  tcp::endpoint peer_d, peer_u;
  tcp::resolver resolver;
  Queue queue_d, queue_u;
  int retain_count = 0;

  Session()
    : socket_d(g_io_service)
    , socket_u(g_io_service)
    , resolver(g_io_service) {}

  void retain() {
    retain_count++;
  }

  void release() {
    if (!--retain_count) {
      delete this;
    }
  }

  void accept(tcp::acceptor &acceptor, std::function<void()> cb) {
    acceptor.async_accept(socket_d, peer_d, [=](const std::error_code &ec) {
      if (ec) {
        std::cerr << "async_accept error: " << ec.message() << std::endl;
      } else {
        resolve();
      }
      cb();
      release();
    });
    retain();
  }

  void resolve() {
    resolver.async_resolve(
      tcp::resolver::query(g_target_addr, g_target_port),
      [this](
        const std::error_code &ec,
        tcp::resolver::results_type result
      ) {
        if (ec) {
          std::cerr << "async_resolve error: " << ec.message() << std::endl;
          close();
        } else {
          peer_u = *result;
          connect();
        }
        release();
      }
    );
    retain();
  }

  void connect() {
    socket_u.async_connect(peer_u, [this](const std::error_code &ec) {
      if (ec) {
        std::cerr << "async_connect error: " << ec.message() << std::endl;
        close();
      } else {
        peer_u = socket_u.remote_endpoint();
        std::cout << "new session "
                  << peer_d.address().to_string() << ':' << peer_d.port() << " => "
                  << peer_u.address().to_string() << ':' << peer_u.port() << std::endl;
        recv_d();
        recv_u();
      }
      release();
    });
    retain();
  }

  void recv_d() {
    auto buf = Buffer::alloc();
    socket_d.async_read_some(
      asio::buffer(buf->data, sizeof(buf->data)),
      [=](const std::error_code &ec, size_t n) {
        if (ec) {
          if (ec == asio::error::eof) {
            std::cout << "downstream EOF" << std::endl;
          } else {
            std::cerr << "downstream async_read_some error: " << ec.message() << std::endl;
          }
          close();
        } else {
          buf->size = n;
          queue_u.push(buf);
          if (auto more = socket_d.available()) {
            while (more > 0) {
              auto buf = Buffer::alloc();
              more -= (buf->size = socket_d.read_some(asio::buffer(buf->data, sizeof(buf->data))));
              queue_u.push(buf);
            }
          }
          send_u();
          recv_d();
        }
        release();
      }
    );
    retain();
  }

  void recv_u() {
    auto buf = Buffer::alloc();
    socket_u.async_read_some(
      asio::buffer(buf->data, sizeof(buf->data)),
      [=](const std::error_code &ec, size_t n) {
        if (ec) {
          if (ec == asio::error::eof) {
            std::cout << "upstream EOF" << std::endl;
          } else {
            std::cerr << "upstream async_read_some error: " << ec.message() << std::endl;
          }
          close();
        } else {
          buf->size = n;
          queue_d.push(buf);
          if (auto more = socket_u.available()) {
            while (more > 0) {
              auto buf = Buffer::alloc();
              more -= (buf->size = socket_u.read_some(asio::buffer(buf->data, sizeof(buf->data))));
              queue_d.push(buf);
            }
          }
          send_d();
          recv_u();
        }
        release();
      }
    );
    retain();
  }

  void send_d() {
    if (auto buf = queue_d.shift()) {
      asio::async_write(
        socket_d,
        asio::buffer(buf->data, buf->size),
        [=](const std::error_code &ec, size_t n) {
          Buffer::free(buf);
          if (ec) {
            std::cerr << "downstream async_write error: " << ec.message() << std::endl;
            close();
          } else {
            send_d();
          }
          release();
        }
      );
      retain();
    }
  }

  void send_u() {
    if (auto buf = queue_u.shift()) {
      asio::async_write(
        socket_u,
        asio::buffer(buf->data, buf->size),
        [=](const std::error_code &ec, size_t n) {
          Buffer::free(buf);
          if (ec) {
            std::cerr << "upstream async_write error: " << ec.message() << std::endl;
            close();
          } else {
            send_u();
          }
          release();
        }
      );
      retain();
    }
  }

  void close() {
    asio::error_code ec;
    socket_d.close(ec);
    socket_u.close(ec);
  }
};

//
// show_error()
//

void show_error(const char *msg) {
  std::cerr << "ERROR: " << msg << std::endl;
  std::cerr << "Usage: baseline [<listen port> [<target port> [<target address>]]]" << std::endl;
}

//
// parse_args()
//

bool parse_args(int argc, char *argv[]) {
  if (argc > 1) {
    auto port = std::atoi(argv[1]);
    if (port <= 0 || port > 65535) {
      show_error("invalid listen port");
      return false;
    }
    g_listen_port = std::to_string(port);
  }

  if (argc > 2) {
    auto port = std::atoi(argv[2]);
    if (port <= 0 || port > 65535) {
      show_error("invalid target port");
      return false;
    }
    g_target_port = std::to_string(port);
  }

  if (argc > 3) {
    g_target_addr = argv[3];
  }

  return true;
}

//
// main()
//

int main(int argc, char *argv[]) {
  if (!parse_args(argc, argv)) return -1;

  tcp::resolver resolver(g_io_service);
  tcp::resolver::query query("0.0.0.0", g_listen_port);
  tcp::endpoint endpoint = *resolver.resolve(query);

  tcp::acceptor acceptor(g_io_service);
  acceptor.open(endpoint.protocol());
  acceptor.set_option(asio::socket_base::reuse_address(true));
  acceptor.bind(endpoint);
  acceptor.listen(asio::socket_base::max_connections);

  std::function<void()> accept;
  accept = [&]() {
    auto session = new Session();
    session->accept(acceptor, [&]() {
      accept();
    });
  };

  accept();

  std::cout << "Listening on port " << g_listen_port << std::endl;
  std::cout << "Proyxing to " << g_target_addr << ':' << g_target_port << std::endl;

  g_io_service.run();

  return 0;
}
