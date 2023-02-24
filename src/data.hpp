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

#ifndef DATA_HPP
#define DATA_HPP

#include "event.hpp"
#include "constants.hpp"
#include "list.hpp"
#include "utils.hpp"

#include <cstring>
#include <functional>
#include <atomic>

namespace pipy {

class SharedData;

//
// Data
//

class Data : public EventTemplate<Data> {
private:
  struct Chunk;
  struct View;

public:
  static const Type __TYPE = Event::Data;

  enum class Encoding {
    UTF8,
    Hex,
    Base64,
    Base64Url,
  };

  //
  // Data::Producer
  //

  class Producer : public List<Producer>::Item {
  public:
    static void for_each(const std::function<void(Producer*)> &cb) {
      for (auto p = s_all_producers.head(); p; p = p->next()) {
        cb(p);
      }
    }

    Producer(const std::string &name) : m_name(pjs::Str::make(name)) {
      s_all_producers.push(this);
    }

    auto name() const -> pjs::Str* { return m_name; }
    auto peak() const -> int { return m_peak; }
    auto current() const -> int { return m_current; }

    Data* make(int size) { return Data::make(size, this); }
    Data* make(int size, int value) { return Data::make(size, value, this); }
    Data* make(const void *data, int size) { return Data::make(data, size, this); }
    Data* make(const std::string &str) { return Data::make(str, this); }
    Data* make(const std::string &str, Encoding encoding) { return Data::make(str, encoding, this); }

    void push(Data *data, const Data *data2) { data->push(*data2); }
    void push(Data *data, const void *p, int n) { data->push(p, n, this); }
    void push(Data *data, const std::string &str) { data->push(str, this); }
    void push(Data *data, const char *str) { data->push(str, this); }
    void push(Data *data, char ch) { data->push(ch, this); }

    void pack(Data *data, const Data *appendant, double vacancy = 0.5) { data->pack(*appendant, this, vacancy); }

  private:
    pjs::Ref<pjs::Str> m_name;
    int m_peak;
    int m_current;

    void increase() { if (++m_current > m_peak) m_peak = m_current; }
    void decrease() { m_current--; }

    thread_local static List<Producer> s_all_producers;

    friend struct Chunk;
  };

  //
  // Data::Builder
  //

  class Builder {
  public:
    Builder(Data &data, Producer *producer)
      : m_data(data)
      , m_buffer(new Data)
      , m_producer(producer)
      , m_chunk(new Chunk(producer)) {}

    ~Builder() {
      delete m_buffer;
      delete m_chunk;
    }

    void flush() {
      if (m_ptr > 0) {
        m_data.push_view(new View(m_chunk, 0, m_ptr));
        m_chunk = new Chunk(m_producer);
        m_ptr = 0;
      }
    }

    void push(char c) {
      m_chunk->data[m_ptr++] = c;
      if (m_ptr >= sizeof(m_chunk->data)) {
        flush();
      }
    }

    void push(uint8_t c) {
      push((char)c);
    }

    void push(int c) {
      push((char)c);
    }

    void push(const char *s, int n) {
      auto &p = m_ptr;
      while (n > 0) {
        int l = sizeof(m_chunk->data) - p;
        if (l > n) l = n;
        std::memcpy(m_chunk->data + p, s, l);
        s += l;
        p += l;
        n -= l;
        if (p >= sizeof(m_chunk->data)) {
          m_data.push_view(new View(m_chunk, 0, p));
          m_chunk = new Chunk(m_producer);
          p = 0;
        }
      }
    }

    void push(const void *p, int n) {
      push((const char *)p, n);
    }

    void push(const char *s) {
      push(s, std::strlen(s));
    }

    void push(const std::string &s) {
      push(s.c_str(), s.length());
    }

    void push(const Data &d) {
      d.to_chunks(
        [this](const uint8_t *p, int n) {
          push(p, n);
        }
      );
    }

    void push(Data &&d) {
      flush();
      m_data.push(std::move(d));
    }

  private:
    Data& m_data;
    Data* m_buffer;
    Producer* m_producer;
    Chunk* m_chunk;
    int m_ptr = 0;
  };

  //
  // Data::Reader
  //

  class Reader {
  public:
    Reader(const Data &data)
      : m_view(data.m_head) {}

    bool eof() {
      return !m_view;
    }

    int get() {
      auto v = m_view;
      if (!v) return -1;
      uint8_t c = v->chunk->data[v->offset + m_offset];
      if (++m_offset >= v->length) {
        m_view = v->next;
        m_offset = 0;
      }
      return c;
    }

    int read(int n, void *out) {
      auto *p = (char *)out;
      int i = 0;
      while (i < n) {
        auto v = m_view;
        if (!v) break;
        auto a = v->length - m_offset;
        auto b = n - i;
        if (a <= b) {
          std::memcpy(p, v->chunk->data + v->offset + m_offset, a);
          p += a;
          i += a;
          m_view = v->next;
          m_offset = 0;
        } else {
          std::memcpy(p, v->chunk->data + v->offset + m_offset, b);
          p += b;
          i += b;
          m_offset += b;
        }
      }
      return i;
    }

    int read(int n, Data &out) {
      int i = 0;
      while (i < n) {
        auto v = m_view;
        if (!v) break;
        auto a = v->length - m_offset;
        auto b = n - i;
        if (a <= b) {
          out.push_view(new View(v->chunk, v->offset + m_offset, a));
          i += a;
          m_view = v->next;
          m_offset = 0;
        } else {
          out.push_view(new View(v->chunk, v->offset + m_offset, b));
          i += b;
          m_offset += b;
        }
      }
      return i;
    }

    int read(Data &out) {
      int n = 0;
      auto v = m_view;
      while (v) {
        auto l = v->length - m_offset;
        n += l;
        out.push_view(new View(v->chunk, v->offset + m_offset, l));
        v = v->next;
        m_offset = 0;
      }
      m_view = nullptr;
      return n;
    }

  private:
    View* m_view;
    int m_offset = 0;
  };

private:

  //
  // Data::Chunk
  //

  struct Chunk : public Pooled<Chunk> {
    std::atomic<int> retain_count;
    char data[DATA_CHUNK_SIZE];

    Chunk(Producer *producer) : retain_count(0), m_producer(producer) { producer->increase(); }
    ~Chunk() { m_producer->decrease(); }

    auto size() const -> int { return sizeof(data); }
    void retain() { retain_count.fetch_add(1, std::memory_order_relaxed); }
    void release() { if (retain_count.fetch_sub(1, std::memory_order_relaxed) == 1) delete this; }

  private:
    Producer* m_producer;
  };

  //
  // Data::View
  //

  struct View : public Pooled<View> {
    View*  prev = nullptr;
    View*  next = nullptr;
    Chunk* chunk;
    int    offset;
    int    length;

    View(Chunk *c, int o, int l)
      : chunk(c)
      , offset(o)
      , length(l)
    {
      c->retain();
    }

    View(View *view)
      : chunk(view->chunk)
      , offset(view->offset)
      , length(view->length)
    {
      chunk->retain();
    }

    ~View() {
      chunk->release();
    }

    int push(const void *p, int n) {
      int tail = offset + length;
      int room = std::min(chunk->size() - tail, n);
      if (room > 0) {
        std::memcpy(chunk->data + tail, p, room);
        length += room;
        return room;
      } else {
        return 0;
      }
    }

    View* pop(int n) {
      length -= n;
      return new View(chunk, offset + length, n);
    }

    View* shift(int n) {
      auto view = new View(chunk, offset, n);
      offset += n;
      length -= n;
      return view;
    }

    View* clone(Producer *producer) {
      if (!producer) producer = &s_unknown_producer;
      auto new_chunk = new Chunk(producer);
      std::memcpy(new_chunk->data, chunk->data + offset, length);
      return new View(new_chunk, 0, length);
    }
  };

public:
  static bool is_flush(const Event *evt) {
    auto data = evt->as<Data>();
    return data && data->empty();
  }

  //
  // Data::Chunks
  //

  class Chunks {
    friend class Data;
    View* m_head;
    Chunks(View *head) : m_head(head) {}

  public:
    class Iterator {
      friend class Chunks;
      View* m_p;
      Iterator(View *p) : m_p(p) {}

    public:
      auto operator++() -> Iterator& {
        m_p = m_p->next;
        return *this;
      }

      bool operator==(Iterator other) const {
        return m_p == other.m_p;
      }

      bool operator!=(Iterator other) const {
        return m_p != other.m_p;
      }

      auto operator*() const -> std::tuple<char*, int> {
        return std::make_tuple(m_p->chunk->data + m_p->offset, m_p->length);
      }
    };

    auto begin() const -> Iterator {
      return Iterator(m_head);
    }

    auto end() const -> Iterator {
      return Iterator(nullptr);
    }
  };

  Data()
    : m_head(nullptr)
    , m_tail(nullptr)
    , m_size(0) {}

  Data(int size, Producer *producer)
    : m_head(nullptr)
    , m_tail(nullptr)
    , m_size(0)
  {
    if (!producer) producer = &s_unknown_producer;
    while (size > 0) {
      auto chunk = new Chunk(producer);
      auto length = std::min(size, chunk->size());
      push_view(new View(chunk, 0, length));
      size -= length;
    }
  }

  Data(int size, int value, Producer *producer)
    : m_head(nullptr)
    , m_tail(nullptr)
    , m_size(0)
  {
    if (!producer) producer = &s_unknown_producer;
    while (size > 0) {
      auto chunk = new Chunk(producer);
      auto length = std::min(size, chunk->size());
      std::memset(chunk->data, value, length);
      push_view(new View(chunk, 0, length));
      size -= length;
    }
  }

  Data(const void *data, int size, Producer *producer)
    : m_head(nullptr)
    , m_tail(nullptr)
    , m_size(0)
  {
    push(data, size, producer);
  }

  Data(const std::string &str, Producer *producer) : Data(str.c_str(), str.length(), producer)
  {
  }

  Data(const std::string &str, Encoding encoding, Producer *producer) : Data() {
    switch (encoding) {
      case Encoding::UTF8:
        push(str, producer);
        break;
      case Encoding::Hex: {
        if (str.length() % 2) throw std::runtime_error("incomplete hex string");
        Builder db(*this, producer);
        utils::HexDecoder decoder([&](uint8_t b) { db.push(b); });
        for (auto c : str) {
          if (!decoder.input(c)) {
            throw std::runtime_error("invalid hex encoding");
          }
        }
        db.flush();
        break;
      }
      case Encoding::Base64: {
        if (str.length() % 4) throw std::runtime_error("incomplete Base64 string");
        Builder db(*this, producer);
        utils::Base64Decoder decoder([&](uint8_t b) { db.push(b); });
        for (auto c : str) {
          if (!decoder.input(c)) {
            throw std::runtime_error("invalid Base64 encoding");
          }
        }
        if (!decoder.complete()) throw std::runtime_error("incomplete Base64 encoding");
        db.flush();
        break;
      }
      case Encoding::Base64Url: {
        Builder db(*this, producer);
        utils::Base64UrlDecoder decoder([&](uint8_t b) { db.push(b); });
        for (auto c : str) {
          if (!decoder.input(c)) {
            throw std::runtime_error("invalid Base64 encoding");
          }
        }
        if (!decoder.flush()) {
          throw std::runtime_error("invalid Base64 encoding");
        }
        db.flush();
        break;
      }
    }
  }

  Data(const Data &other)
    : m_head(nullptr)
    , m_tail(nullptr)
    , m_size(0)
  {
    for (auto view = other.m_head; view; view = view->next) {
      push_view(new View(view));
    }
  }

  Data(Data &&other)
    : m_head(other.m_head)
    , m_tail(other.m_tail)
    , m_size(other.m_size)
  {
    other.m_head = nullptr;
    other.m_tail = nullptr;
    other.m_size = 0;
  }

  Data(const SharedData &other);

  ~Data() {
    clear();
  }

  auto operator=(const Data &other) -> Data& {
    assert_same_thread(*this);
    assert_same_thread(other);
    if (this != &other) {
      clear();
      for (auto view = other.m_head; view; view = view->next) {
        push_view(new View(view));
      }
    }
    return *this;
  }

  auto operator=(Data &&other) -> Data& {
    assert_same_thread(*this);
    assert_same_thread(other);
    if (this != &other) {
      for (auto *p = m_head; p; ) {
        auto *v = p; p = p->next;
        delete v;
      }
      m_head = other.m_head;
      m_tail = other.m_tail;
      m_size = other.m_size;
      other.m_head = nullptr;
      other.m_tail = nullptr;
      other.m_size = 0;
    }
    return *this;
  }

  bool empty() const {
    assert_same_thread(*this);
    return !m_size;
  }

  int size() const {
    assert_same_thread(*this);
    return m_size;
  }

  Chunks chunks() const {
    assert_same_thread(*this);
    return Chunks(m_head);
  }

  void clear() {
    assert_same_thread(*this);
    for (auto *p = m_head; p; ) {
      auto *v = p; p = p->next;
      delete v;
    }
    m_head = m_tail = nullptr;
    m_size = 0;
  }

  void push(const Data &data) {
    assert_same_thread(*this);
    if (&data == this) return;
    for (auto view = data.m_head; view; view = view->next) {
      push_view(new View(view));
    }
  }

  void push(Data &&data) {
    assert_same_thread(*this);
    if (&data == this) return;
    if (!data.m_head) return;
    if (m_tail) {
      data.m_head->prev = m_tail;
      m_tail->next = data.m_head;
    } else {
      m_head = data.m_head;
    }
    m_size += data.m_size;
    m_tail = data.m_tail;
    data.m_head = data.m_tail = nullptr;
    data.m_size = 0;
  }

  void push(const std::string &str, Producer *producer) {
    assert_same_thread(*this);
    push(str.c_str(), str.length(), producer);
  }

  void push(const char *str, Producer *producer) {
    assert_same_thread(*this);
    push(str, std::strlen(str), producer);
  }

  void push(const void *data, int n, Producer *producer) {
    assert_same_thread(*this);
    if (!producer) producer = &s_unknown_producer;
    const char *p = (const char*)data;
    if (auto view = m_tail) {
      auto chunk = view->chunk;
      if (chunk->retain_count == 1) {
        int added = view->push(p, n);
        m_size += added;
        p += added;
        n -= added;
      }
    }
    while (n > 0) {
      auto view = new View(new Chunk(producer), 0, 0);
      auto added = view->push(p, n);
      p += added;
      n -= added;
      push_view(view);
    }
  }

  void push(char ch, Producer *producer) {
    assert_same_thread(*this);
    if (auto tail = m_tail) {
      auto chunk = tail->chunk;
      if (chunk->retain_count == 1) {
        int end = tail->offset + tail->length;
        if (end < chunk->size()) {
          chunk->data[end] = ch;
          tail->length++;
          m_size++;
          return;
        }
      }
    }
    auto chunk = new Chunk(producer ? producer : &s_unknown_producer);
    auto view = new View(chunk, 0, 1);
    chunk->data[0] = ch;
    push_view(view);
  }

  void scan(const std::function<bool(int)> &f) {
    assert_same_thread(*this);
    for (auto view = m_head; view; view = view->next) {
      auto data = view->chunk->data;
      auto size = view->length;
      auto head = view->offset;
      for (int i = 0; i < size; ++i) if (!f(data[head + i])) return;
    }
  }

  void pop(int n) {
    assert_same_thread(*this);
    while (auto view = m_tail) {
      if (n <= 0) break;
      if (view->length <= n) {
        n -= view->length;
        delete pop_view();
      } else {
        delete view->pop(n);
        m_size -= n;
        break;
      }
    }
  }

  void pop(int n, Data &out) {
    assert_same_thread(*this);
    assert_same_thread(out);
    while (auto view = m_tail) {
      if (n <= 0) break;
      if (view->length <= n) {
        n -= view->length;
        out.unshift_view(pop_view());
      } else {
        out.unshift_view(view->pop(n));
        m_size -= n;
        break;
      }
    }
  }

  void shift(int n) {
    assert_same_thread(*this);
    while (auto view = m_head) {
      if (n <= 0) break;
      if (view->length <= n) {
        n -= view->length;
        delete shift_view();
      } else {
        delete view->shift(n);
        m_size -= n;
        break;
      }
    }
  }

  void shift(int n, uint8_t *out) {
    assert_same_thread(*this);
    auto i = 0;
    while (auto view = m_head) {
      if (n <= 0) break;
      auto p = view->chunk->data + view->offset;
      auto l = view->length;
      if (l <= n) {
        std::memcpy(out + i, p, l);
        i += l;
        n -= l;
        delete shift_view();
      } else {
        std::memcpy(out + i, p, n);
        i += n;
        delete view->shift(n);
        m_size -= n;
        break;
      }
    }
  }

  void shift(int n, Data &out) {
    assert_same_thread(*this);
    assert_same_thread(out);
    while (auto view = m_head) {
      if (n <= 0) break;
      if (view->length <= n) {
        n -= view->length;
        out.push_view(shift_view());
      } else {
        out.push_view(view->shift(n));
        m_size -= n;
        break;
      }
    }
  }

  void shift(const std::function<int(int)> &f, Data &out) {
    assert_same_thread(*this);
    assert_same_thread(out);
    while (auto view = m_head) {
      auto data = view->chunk->data;
      auto size = view->length;
      auto head = view->offset;
      auto n = 0;
      auto br = false;
      for (; n < size; ++n) {
        if (auto ret = f(data[head + n])) {
          br = true;
          if (ret > 0) n++;
          break;
        }
      }
      if (n == size) {
        out.push_view(shift_view());
      } else {
        out.push_view(view->shift(n));
        m_size -= n;
      }
      if (br) break;
    }
  }

  void shift_while(const std::function<bool(int)> &f, Data &out) {
    assert_same_thread(*this);
    assert_same_thread(out);
    while (auto view = m_head) {
      auto data = view->chunk->data;
      auto size = view->length;
      auto head = view->offset;
      auto n = 0;
      auto br = false;
      for (; n < size; n++) {
        if (!f(data[head + n])) {
          br = true;
          break;
        }
      }
      if (n == size) {
        out.push_view(shift_view());
      } else {
        out.push_view(view->shift(n));
        m_size -= n;
      }
      if (br) break;
    }
  }

  void shift_to(const std::function<bool(int)> &f, Data &out) {
    assert_same_thread(*this);
    assert_same_thread(out);
    while (auto view = m_head) {
      auto data = view->chunk->data;
      auto size = view->length;
      auto head = view->offset;
      auto n = 0;
      auto br = false;
      for (; n < size; n++) {
        if (f(data[head + n])) {
          br = true;
          n++;
          break;
        }
      }
      if (n == size) {
        out.push_view(shift_view());
      } else {
        out.push_view(view->shift(n));
        m_size -= n;
      }
      if (br) break;
    }
  }

  void pack(const Data &data, Producer *producer, double vacancy = 0.5);

  void to_chunks(const std::function<void(const uint8_t*, int)> &cb) const {
    assert_same_thread(*this);
    for (auto view = m_head; view; view = view->next) {
      cb((uint8_t*)view->chunk->data + view->offset, view->length);
    }
  }

  void to_bytes(uint8_t *buf) const {
    assert_same_thread(*this);
    auto p = buf;
    for (auto view = m_head; view; view = view->next) {
      auto length = view->length;
      std::memcpy(p, view->chunk->data + view->offset, length);
      p += length;
    }
  }

  void to_bytes(uint8_t *buf, size_t len) const {
    assert_same_thread(*this);
    auto ptr = buf;
    for (auto view = m_head; view && len > 0; view = view->next) {
      auto length = view->length;
      auto n = length < len ? length : len;
      std::memcpy(ptr, view->chunk->data + view->offset, n);
      len -= n;
      ptr += n;
    }
  }

  void to_bytes(std::vector<uint8_t> &buf) const {
    buf.resize(size());
    to_bytes(&buf[0]);
  }

  auto to_bytes() const -> std::vector<uint8_t> {
    std::vector<uint8_t> ret;
    to_bytes(ret);
    return ret;
  }

  virtual auto to_string() const -> std::string override {
    assert_same_thread(*this);
    auto size = m_size;
    if (size > pjs::Str::max_size()) size = pjs::Str::max_size();
    std::string str(size, 0);
    int i = 0, n = size;
    for (auto view = m_head; n > 0 && view; view = view->next) {
      auto length = view->length;
      if (length > n) length = n;
      str.replace(i, length, view->chunk->data + view->offset, length);
      i += length;
      n -= length;
    }
    return str;
  }

  auto to_string(Encoding encoding) const -> std::string {
    assert_same_thread(*this);
    switch (encoding) {
      case Encoding::UTF8: {
        pjs::Utf8Decoder decoder([](int) {});
        for (const auto c : chunks()) {
          auto ptr = std::get<0>(c);
          auto len = std::get<1>(c);
          for (int i = 0; i < len; i++) {
            if (!decoder.input(ptr[i])) {
              throw std::runtime_error("invalid UTF-8 encoding");
            }
          }
        }
        return to_string();
      }
      case Encoding::Hex: {
        std::string str;
        utils::HexEncoder encoder([&](char c) { str += c; });
        for (const auto c : chunks()) {
          auto ptr = std::get<0>(c);
          auto len = std::get<1>(c);
          for (int i = 0; i < len; i++) encoder.input(ptr[i]);
        }
        return str;
      }
      case Encoding::Base64: {
        std::string str;
        utils::Base64Encoder encoder([&](char c) { str += c; });
        for (const auto c : chunks()) {
          auto ptr = std::get<0>(c);
          auto len = std::get<1>(c);
          for (int i = 0; i < len; i++) encoder.input(ptr[i]);
        }
        encoder.flush();
        return str;
      }
      case Encoding::Base64Url: {
        std::string str;
        utils::Base64UrlEncoder encoder([&](char c) { str += c; });
        for (const auto c : chunks()) {
          auto ptr = std::get<0>(c);
          auto len = std::get<1>(c);
          for (int i = 0; i < len; i++) encoder.input(ptr[i]);
        }
        encoder.flush();
        return str;
      }
      default: return to_string();
    }
  }

private:
  View*  m_head;
  View*  m_tail;
  int    m_size;

  void push_view(View *view) {
    auto size = view->length;
    if (auto tail = m_tail) {
      if (tail->chunk == view->chunk &&
          tail->offset + tail->length == view->offset)
      {
        delete view;
        tail->length += size;
        m_size += size;
        return;
      }
      tail->next = view;
      view->prev = tail;
    } else {
      m_head = view;
    }
    m_tail = view;
    m_size += size;
  }

  auto pop_view() -> View* {
    auto view = m_tail;
    m_tail = view->prev;
    view->prev = nullptr;
    if (m_tail) {
      m_tail->next = nullptr;
    } else {
      m_head = nullptr;
    }
    m_size -= view->length;
    return view;
  }

  auto shift_view() -> View* {
    auto view = m_head;
    m_head = view->next;
    view->next = nullptr;
    if (m_head) {
      m_head->prev = nullptr;
    } else {
      m_tail = nullptr;
    }
    m_size -= view->length;
    return view;
  }

  void unshift_view(View *view) {
    auto size = view->length;
    if (auto head = m_head) {
      if (head->chunk == view->chunk &&
          head->offset == view->offset + size)
      {
        delete view;
        head->offset -= size;
        head->length += size;
        m_size += size;
        return;
      }
      head->prev = view;
      view->next = head;
    } else {
      m_tail = view;
    }
    m_head = view;
    m_size += size;
  }

  thread_local static Producer s_unknown_producer;

  friend class SharedData;
};

//
// SharedData
//

class SharedData : public pjs::Pooled<SharedData> {
public:
  static auto make(const Data &data) -> SharedData* {
    return new SharedData(data);
  }

  void to_data(Data &data) const {
    for (auto *v = m_views; v; v = v->next) {
      data.push_view(new Data::View(
        v->chunk,
        v->offset,
        v->length
      ));
    }
  }

  auto retain() -> SharedData* {
    m_retain_count.fetch_add(1, std::memory_order_relaxed);
    return this;
  }

  void release() {
    if (m_retain_count.fetch_sub(1, std::memory_order_relaxed) == 1) {
      delete this;
    }
  }

private:

  //
  // SharedData::View
  //

  struct View : public pjs::Pooled<View> {
    View* next = nullptr;
    Data::Chunk* chunk;
    int offset;
    int length;

    View(Data::View *v)
      : chunk(v->chunk)
      , offset(v->offset)
      , length(v->length)
    {
      chunk->retain();
    }

    ~View() {
      chunk->release();
    }
  };

  SharedData(const Data &data) : m_retain_count(0) {
    auto **p = &m_views;
    for (auto *v = data.m_head; v; v = v->next) {
      p = &(*p = new View(v))->next;
    }
  }

  ~SharedData() {
    auto *v = m_views;
    while (v) {
      auto *view = v; v = v->next;
      delete view;
    }
  }

  View* m_views = nullptr;
  std::atomic<int> m_retain_count;
};

inline Data::Data(const SharedData &other)
  : m_head(nullptr)
  , m_tail(nullptr)
  , m_size(0)
{
  other.to_data(*this);
}

} // namespace pipy

#endif // DATA_HPP
