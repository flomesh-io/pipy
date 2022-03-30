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

#include "dump.hpp"
#include "context.hpp"
#include "data.hpp"
#include "logging.hpp"

#include <iostream>
#include <ctime>
#include <sstream>

namespace pipy {

Dump::Dump() {
}

Dump::Dump(const pjs::Value &tag)
  : m_tag(tag)
{
}

Dump::Dump(const Dump &r)
  : Filter(r)
  , m_tag(r.m_tag)
{
}

Dump::~Dump() {
}

void Dump::dump(std::ostream &out) {
  out << "dump";
}

auto Dump::clone() -> Filter* {
  return new Dump(*this);
}

void Dump::process(Event *evt) {
  static char s_hex[] = { "0123456789ABCDEF" };

  std::ostream *s = &std::cout;
  auto &o = *s;

  pjs::Value tag;
  if (!eval(m_tag, tag)) {
    output(evt);
    return;
  }

  std::stringstream ss;
  ss << "[dump] [context=" << context()->id() << "] ";

  if (m_tag.to_boolean()) {
    auto s = tag.to_string();
    ss << "[" << s->str() << "] ";
    s->release();
  }

  ss << evt->name();

  if (auto end = evt->as<StreamEnd>()) {
    if (end->error() != StreamEnd::_NO_ERROR) {
      ss << " [" << end->error() << "] " << end->message();
    }
    Log::print(Log::INFO, ss.str());

  } else if (auto data = evt->as<Data>()) {
    if (data->empty()) {
      ss << " Flush";
    } else {
      ss << " [size=" << data->size() << "]";
    }
    Log::print(Log::INFO, ss.str());
    if (!data->empty()) {
      std::string hex, txt;
      std::string hline(16*3+4+16, '-');
      Log::print(hline);
      for (auto chunk : data->chunks()) {
        auto data = std::get<0>(chunk);
        auto size = std::get<1>(chunk);
        for (int i = 0; i < size; ++i) {
          auto ch = (unsigned char)data[i];
          hex += s_hex[ch >> 4];
          hex += s_hex[ch & 15];
          hex += ' ';
          txt += ch < 0x20 || ch >= 0x7f ? '?' : ch;
          if (txt.length() == 16) {
            Log::print(hex + " | " + txt);
            hex.clear();
            txt.clear();
          }
        }
      }
      if (!txt.empty()) {
        std::string line = hex;
        for (int n = 16 - hex.length() / 3; n > 0; --n) line += " - ";
        line += " | ";
        line += txt;
        line += std::string(16 - txt.length(), '.');
        Log::print(line);
      }
      Log::print(hline);
    }

  } else {
    Log::print(Log::INFO, ss.str());
  }

  output(evt);
}

} // namespace pipy
