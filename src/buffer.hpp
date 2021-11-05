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

#ifndef BUFFER_HPP
#define BUFFER_HPP

#include "event.hpp"

#include <list>
#include <string>

namespace pipy {

//
// CharBuf
//

template<int N>
class CharBuf {
  char m_buf[N+1];
  int  m_len = 0;

public:
  void clear() {
    m_len = 0;
  }

  bool empty() const {
    return m_len == 0;
  }

  int length() const { 
    return m_len;
  }

  char operator[](int i) const {
    return m_buf[i];
  }

  auto str() const -> std::string {
    return std::string(m_buf, m_len);
  }

  auto c_str() -> const char* {
    m_buf[m_len] = 0;
    return m_buf;
  }

  void push(char c) {
    if (c < ' ' || (c == ' ' && m_len == 0)) return;
    if (m_len < N) m_buf[m_len++] = c;
  }
};

//
// ByteBuf
//

template<int N>
class ByteBuf {
  char m_buf[N+1];
  int  m_len = 0;

public:
  void clear() {
    m_len = 0;
  }

  bool empty() const {
    return m_len == 0;
  }

  int length() const { 
    return m_len;
  }

  char operator[](int i) const {
    return m_buf[i];
  }

  auto str() const -> std::string {
    return std::string(m_buf, m_len);
  }

  auto c_str() -> const char* {
    m_buf[m_len] = 0;
    return m_buf;
  }

  void push(char c) {
    if (m_len < N) m_buf[m_len++] = c;
  }
};

//
// EventBuffer
//

class EventBuffer {
public:
  bool empty() const {
    return m_buffer.empty();
  }

  void push(Event *e) {
    e->retain();
    m_buffer.push_back(e);
  }

  auto shift() -> Event* {
    if (m_buffer.empty()) return nullptr;
    auto e = m_buffer.front();
    m_buffer.pop_front();
    return e;
  }

  void unshift(Event *e) {
    e->retain();
    m_buffer.push_front(e);
  }

  void flush(const std::function<void(Event*)> &out) {
    std::list<Event*> events(std::move(m_buffer));
    for (auto *e : events) {
      out(e);
      e->release();
    }
  }

  void clear() {
    for (auto *e : m_buffer) {
      e->release();
    }
    m_buffer.clear();
  }

private:
  std::list<Event*> m_buffer;
};

} // namespace pipy

#endif // BUFFER_HPP