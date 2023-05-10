#include <functional>
#include <iostream>
#include <cstdlib>

#define ASIO_STANDALONE
#include <asio.hpp>

#define CONFIG_DATA_CHUNK_SIZE  16*1024
#define CONFIG_TCP_NO_DELAY     1
#define CONFIG_RECV_EXTRA_READ  0
#define CONFIG_WRITE_ASYNC_SOME 0
#define CONFIG_CUSTOM_ALLOCATOR 1

using tcp = asio::ip::tcp;

asio::io_service g_io_service;
std::string g_listen_port("8000");
std::string g_target_port("8080");
std::string g_target_addr("127.0.0.1");

//
// HandlerAllocator
//

template<typename T>
class HandlerAllocator {
public:
  using value_type = T;

  template<typename U>
  struct rebind {
    using other = HandlerAllocator<U>;
  };

  HandlerAllocator() = default;

  template<typename U>
  HandlerAllocator(const HandlerAllocator<U> &other) {};

  T* allocate(size_t n) {
    if (auto p = m_pool) {
      m_pool = *reinterpret_cast<T**>(p);
      return p;
    } else {
      return static_cast<T*>(std::malloc(std::max(sizeof(T), sizeof(T*))));
    }
  }

  void deallocate(T *p, size_t n) {
    *reinterpret_cast<T**>(p) = m_pool;
    m_pool = p;
  }

private:
  thread_local static T* m_pool;
};

template<typename T>
thread_local T* HandlerAllocator<T>::m_pool = nullptr;

//
// Buffer
//

struct Buffer {
  Buffer* next = nullptr;
  size_t size = 0;
  char data[CONFIG_DATA_CHUNK_SIZE];

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
    if (auto p = head) {
      head = p->next;
      if (!head) tail = nullptr;
      return p;
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

  void set_socket_options() {
    socket_d.set_option(asio::socket_base::keep_alive(true));
    socket_u.set_option(asio::socket_base::keep_alive(true));
    socket_d.set_option(tcp::no_delay(CONFIG_TCP_NO_DELAY));
    socket_u.set_option(tcp::no_delay(CONFIG_TCP_NO_DELAY));
  }

  //
  // Session::Handler
  //

  template<typename T>
  struct Handler {
    using allocator_type = HandlerAllocator<T>;
    allocator_type get_allocator() const {
      return allocator_type();
    }

    Session* session;
    Buffer* buffer;

    Handler() = default;
    Handler(Session *s, Buffer *b) : session(s), buffer(b) {}
    Handler(const Handler &r) : session(r.session), buffer(r.buffer) {}
    Handler(Handler &&r) : session(r.session), buffer(r.buffer) {}
  };

  //
  // Session::RecvHandlerD
  //

  struct RecvHandlerD : public Handler<RecvHandlerD> {
    using Handler::Handler;
    void operator()(const std::error_code &ec, size_t n) {
      if (ec) {
        if (ec == asio::error::eof) {
          std::cout << "downstream EOF" << std::endl;
        } else {
          std::cerr << "downstream async_read_some error: " << ec.message() << std::endl;
        }
        session->close();
      } else {
        buffer->size = n;
        session->queue_u.push(buffer);
        if (CONFIG_RECV_EXTRA_READ) {
          if (auto more = session->socket_d.available()) {
            while (more > 0) {
              auto buf = Buffer::alloc();
              more -= (buf->size = session->socket_d.read_some(asio::buffer(buf->data, sizeof(buf->data))));
              session->queue_u.push(buf);
            }
          }
        }
        session->send_u();
        session->recv_d();
      }
      session->release();
    }
  };

  //
  // Session::RecvHandlerU
  //

  struct RecvHandlerU : public Handler<RecvHandlerU> {
    using Handler::Handler;
    void operator()(const std::error_code &ec, size_t n) {
      if (ec) {
        if (ec == asio::error::eof) {
          std::cout << "upstream EOF" << std::endl;
        } else {
          std::cerr << "upstream async_read_some error: " << ec.message() << std::endl;
        }
        session->close();
      } else {
        buffer->size = n;
        session->queue_d.push(buffer);
        if (CONFIG_RECV_EXTRA_READ) {
          if (auto more = session->socket_u.available()) {
            while (more > 0) {
              auto buf = Buffer::alloc();
              more -= (buf->size = session->socket_u.read_some(asio::buffer(buf->data, sizeof(buf->data))));
              session->queue_d.push(buf);
            }
          }
        }
        session->send_d();
        session->recv_u();
      }
      session->release();
    }
  };

  //
  // Session::SendHandlerD
  //

  struct SendHandlerD : public Handler<SendHandlerD> {
    using Handler::Handler;
    void operator()(const std::error_code &ec, size_t n) {
      Buffer::free(buffer);
      if (ec) {
        std::cerr << "downstream async_write error: " << ec.message() << std::endl;
        session->close();
      } else {
        session->send_d();
      }
      session->release();
    }
  };

  //
  // Session::SendHandlerU
  //

  struct SendHandlerU : public Handler<SendHandlerU> {
    using Handler::Handler;
    void operator()(const std::error_code &ec, size_t n) {
      Buffer::free(buffer);
      if (ec) {
        std::cerr << "upstream async_write error: " << ec.message() << std::endl;
        session->close();
      } else {
        session->send_u();
      }
      session->release();
    }
  };

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
        set_socket_options();
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
#if CONFIG_CUSTOM_ALLOCATOR
      RecvHandlerD(this, buf)
#else
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
          if (CONFIG_RECV_EXTRA_READ) {
            if (auto more = socket_d.available()) {
              while (more > 0) {
                auto buf = Buffer::alloc();
                more -= (buf->size = socket_d.read_some(asio::buffer(buf->data, sizeof(buf->data))));
                queue_u.push(buf);
              }
            }
          }
          send_u();
          recv_d();
        }
        release();
      }
#endif // CONFIG_CUSTOM_ALLOCATOR
    );
    retain();
  }

  void recv_u() {
    auto buf = Buffer::alloc();
    socket_u.async_read_some(
      asio::buffer(buf->data, sizeof(buf->data)),
#if CONFIG_CUSTOM_ALLOCATOR
      RecvHandlerU(this, buf)
#else
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
          if (CONFIG_RECV_EXTRA_READ) {
            if (auto more = socket_u.available()) {
              while (more > 0) {
                auto buf = Buffer::alloc();
                more -= (buf->size = socket_u.read_some(asio::buffer(buf->data, sizeof(buf->data))));
                queue_d.push(buf);
              }
            }
          }
          send_d();
          recv_u();
        }
        release();
      }
#endif // CONFIG_CUSTOM_ALLOCATOR
    );
    retain();
  }

  void send_d() {
    if (auto buf = queue_d.shift()) {
#if CONFIG_WRITE_ASYNC_SOME
      socket_d.async_write_some(
#else
      asio::async_write(socket_d,
#endif
        asio::buffer(buf->data, buf->size),
#if CONFIG_CUSTOM_ALLOCATOR
        SendHandlerD(this, buf)
#else
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
#endif // CONFIG_CUSTOM_ALLOCATOR
      );
      retain();
    }
  }

  void send_u() {
    if (auto buf = queue_u.shift()) {
#if CONFIG_WRITE_ASYNC_SOME
      socket_u.async_write_some(
#else
      asio::async_write(socket_u,
#endif
        asio::buffer(buf->data, buf->size),
#if CONFIG_CUSTOM_ALLOCATOR
        SendHandlerU(this, buf)
#else
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
#endif // CONFIG_CUSTOM_ALLOCATOR
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
