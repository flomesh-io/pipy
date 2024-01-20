/*
 *  Copyright (c) 2019 by flomesh.io
 *
 *  Unless prior written consent has been obtained from the copyright
 *  owner, the following shall not be allowed.
 *
 *  1. The distribution of any source codes, header files, make files,
 *     or libraries of the software.
 *
 *  2. Disclosure of any source codes pertaining to the software to any
 *     additional parties.
 *
 *  3. Alteration or removal of any notices in or on the software or
 *     within the documentation included within the software.
 *
 *  ALL SOURCE CODE AS WELL AS ALL DOCUMENTATION INCLUDED WITH THIS
 *  SOFTWARE IS PROVIDED IN AN “AS IS” CONDITION, WITHOUT WARRANTY OF ANY
 *  KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef NET_HPP
#define NET_HPP

#include "data.hpp"
#include "allocator.hpp"

#define ASIO_STANDALONE
#include <asio.hpp>

#include "os-platform.hpp"

namespace pipy {

//
// Net
//

class Net {
public:
  static void init();

  static auto main() -> Net& {
    return *s_main;
  }

  static auto current() -> Net& {
    return s_current;
  }

  static auto context() -> asio::io_context& {
    return s_current.m_io_context;
  }

  static bool is_main() { return &s_current == s_main; }

  auto io_context() -> asio::io_context& { return m_io_context; }
  bool is_running() const { return m_is_running; }

  void run();
  auto run_one() -> size_t;
  void stop();
  void restart();
  void post(const std::function<void()> &cb);
  void defer(const std::function<void()> &cb);

private:
  asio::io_context m_io_context;
  bool m_is_running;
  static Net* s_main;
  static thread_local Net s_current;
};

//
// Asio buffer array adapter
//

class DataChunks {
  Data::Chunks m_chunks;

public:
  class Iterator {
    Data::Chunks::Iterator m_i;

  public:
    Iterator(const Data::Chunks::Iterator &i) : m_i(i) {}
    bool operator==(const Iterator &other) const { return m_i == other.m_i; }
    bool operator!=(const Iterator &other) const { return m_i != other.m_i; }
    auto operator++() -> Iterator& { ++m_i; return *this; }
    auto operator*() -> asio::mutable_buffer {
      auto t = *m_i;
      return asio::mutable_buffer(
        std::get<0>(t),
        std::get<1>(t)
      );
    }
    auto operator*() const -> asio::const_buffer {
      auto t = *m_i;
      return asio::const_buffer(
        std::get<0>(t),
        std::get<1>(t)
      );
    }
  };

  DataChunks(const Data::Chunks &chunks) : m_chunks(chunks) {}
  auto begin() const -> Iterator { return Iterator(m_chunks.begin()); }
  auto end() const -> Iterator { return Iterator(m_chunks.end()); }
};

//
// SelfHandler
//

template<typename T>
struct SelfHandler {
  using allocator_type = PooledAllocator<SelfHandler>;

  allocator_type get_allocator() const {
    return allocator_type();
  }

  SelfHandler() = default;
  SelfHandler(T *s) : self(s) {}
  SelfHandler(const SelfHandler &r) : self(r.self) {}
  SelfHandler(SelfHandler &&r) : self(r.self) {}

  T* self;
};

//
// SelfDataHandler
//

template<typename T, typename U>
struct SelfDataHandler {
  using allocator_type = PooledAllocator<SelfDataHandler>;

  allocator_type get_allocator() const {
    return allocator_type();
  }

  SelfDataHandler() = default;
  SelfDataHandler(T *s, U *d) : self(s), data(d) {}
  SelfDataHandler(const SelfDataHandler &r) : self(r.self), data(r.data) {}
  SelfDataHandler(SelfDataHandler &&r) : self(r.self), data(r.data) {}

  T* self;
  U* data;
};

//
// SelfHandlerMT
//

template<typename T>
struct SelfHandlerMT {
  using allocator_type = PooledAllocatorMT<SelfHandlerMT>;

  allocator_type get_allocator() const {
    return allocator_type();
  }

  SelfHandlerMT() = default;
  SelfHandlerMT(T *s) : self(s) {}
  SelfHandlerMT(const SelfHandlerMT &r) : self(r.self) {}
  SelfHandlerMT(SelfHandlerMT &&r) : self(r.self) {}

  T* self;
};

//
// SelfDataHandlerMT
//

template<typename T, typename U>
struct SelfDataHandlerMT {
  using allocator_type = PooledAllocatorMT<SelfDataHandlerMT>;

  allocator_type get_allocator() const {
    return allocator_type();
  }

  SelfDataHandlerMT() = default;
  SelfDataHandlerMT(T *s, U *d) : self(s), data(d) {}
  SelfDataHandlerMT(const SelfDataHandlerMT &r) : self(r.self), data(r.data) {}
  SelfDataHandlerMT(SelfDataHandlerMT &&r) : self(r.self), data(r.data) {}

  T* self;
  U* data;
};

//
// SelfTask
//

template<typename T, typename S>
class SelfTask : public pjs::Pooled<T>, public pjs::RefCount<T> {
private:
  S* m_self;

public:
  SelfTask(S *self) : m_self(self) {
    pjs::RefCount<T>::retain();
    Net::current().post(
      [this]() {
        if (m_self) {
          static_cast<T*>(this)->execute(m_self);
        }
        pjs::RefCount<T>::release();
      }
    );
  }

  void cancel() { m_self = nullptr; }
};

} // namespace pipy

namespace asio {

template<>
class is_const_buffer_sequence<pipy::DataChunks> {
public:
  const static bool value = true;
};

} // namespace asio

namespace std {

template<>
class iterator_traits<pipy::DataChunks::Iterator> {
public:
  typedef size_t difference_type;
};

template<>
inline void advance(pipy::DataChunks::Iterator &i, size_t n) {
  while (n-- > 0) ++i;
}

} // namespace std

#endif // NET_HPP
