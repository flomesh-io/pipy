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

#include "context.hpp"

#include <cstring>
#include <chrono>

NS_BEGIN

//
// Context
//

uint64_t Context::s_context_id = 0;

Context::Context() {
  if (!++s_context_id) s_context_id++;
  id = s_context_id;
}

Context::Context(const Context &rhs) : Context() {
  remote_addr = rhs.remote_addr;
  remote_port = rhs.remote_port;
  local_addr = rhs.local_addr;
  local_port = rhs.local_port;
  variables = rhs.variables;
}

Context::~Context() {
}

auto Context::evaluate(const std::string &str, bool *solved) const -> std::string {
  std::string result;
  if (solved) *solved = true;
  for (size_t i = 0; i < str.length(); ++i) {
    auto ch = str[i];
    if (ch == '$' && str[i+1] == '{') {
      size_t s = i + 2, j = s;
      while (j < str.length() && str[j] != '}') ++j;
      std::string name(str.c_str() + s, j - s);
      if (name.length() > 2 && name[0] == '_' && name[1] == '_') {
        if (name == "__remote_addr") result += remote_addr;
        else if (name == "__remote_port") result += std::to_string(remote_port);
        else if (name == "__local_addr") result += local_addr;
        else if (name == "__local_port") result += std::to_string(local_port);
        else if (solved) *solved = false;
      } else {
        auto it = variables.find(name);
        if (it == variables.end()) {
          if (solved) *solved = false;
        } else {
          result += it->second;
        }
      }
      i = j;
    } else {
      result += ch;
    }
  }
  return result;
}

//
// Context::Queue
//

void Context::Queue::clear() {
  m_buffer.clear();
}

void Context::Queue::send(std::unique_ptr<Object> obj) {
  for (const auto &out : m_receivers) {
    out(clone_object(obj));
  }
  m_buffer.push_back(std::move(obj));
}

void Context::Queue::receive(Object::Receiver receiver) {
  m_receivers.push_back(receiver);
  for (const auto &obj : m_buffer) {
    receiver(clone_object(obj));
  }
}

NS_END
