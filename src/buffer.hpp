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

#include "ns.hpp"
#include "object.hpp"

#include <list>
#include <string>

NS_BEGIN

struct Object;

//
// CharBuf
//

template<int N>
class CharBuf {
  char m_buf[N];
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
  char m_buf[N];
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

  void push(char c) {
    if (m_len < N) m_buf[m_len++] = c;
  }
};

//
// Buffer
//

class Buffer {
public:
  Buffer() {}

  Buffer(Buffer &&rhs) {
    m_objects = std::move(rhs.m_objects);
  }

  void operator=(Buffer &&rhs) {
    m_objects = std::move(rhs.m_objects);
  }

  auto size() const -> size_t {
    return m_objects.size();
  }

  void clear() {
    m_objects.clear();
  }

  auto first() const -> const std::unique_ptr<Object>& {
    return m_objects.front();
  }

  void push(std::unique_ptr<Object> obj) {
    m_objects.push_back(std::move(obj));
  }

  auto pop() -> std::unique_ptr<Object> {
    if (m_objects.size() == 0) return nullptr;
    auto obj = std::move(m_objects.back());
    m_objects.pop_back();
    return obj;
  }

  auto shift() -> std::unique_ptr<Object> {
    if (m_objects.size() == 0) return nullptr;
    auto obj = std::move(m_objects.front());
    m_objects.pop_front();
    return obj;
  }

  void send(Object::Receiver out) {
    for (const auto &obj : m_objects) {
      out(clone_object(obj));
    }
  }

private:
  std::list<std::unique_ptr<Object>> m_objects;
};

NS_END

#endif // BUFFER_HPP
