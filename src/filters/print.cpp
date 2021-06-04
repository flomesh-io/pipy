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

#include "print.hpp"
#include "data.hpp"
#include "logging.hpp"

#include <stdio.h>

namespace pipy {

Print::Print() {
}

Print::Print(const Print &r)
  : Print()
{
}

Print::~Print() {
}

auto Print::help() -> std::list<std::string> {
  return {
    "print()",
    "Outputs raw data to the standard output",
  };
}

void Print::dump(std::ostream &out) {
  out << "print";
}

auto Print::clone() -> Filter* {
  return new Print(*this);
}

void Print::reset() {
}

void Print::process(Context *ctx, Event *inp) {
  if (auto *data = inp->as<Data>()) {
    for (auto chunk : data->chunks()) {
      auto data = std::get<0>(chunk);
      auto size = std::get<1>(chunk);
      for (size_t i = 0; i < size; i++) {
        if (data[i] == '\n') {
          Log::print(m_line);
          m_line.clear();
        } else if (data[i] >= ' ') {
          m_line += data[i];
          if (m_line.length() >= 100) {
            Log::print(m_line);
            m_line.clear();
          }
        }
      }
    }
  } else if (inp->is<MessageEnd>() || inp->is<SessionEnd>()) {
    if (!m_line.empty()) {
      Log::print(m_line);
      m_line.clear();
    }
  }
  output(inp);
}

} // namespace pipy