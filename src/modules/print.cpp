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
#include "utils.hpp"

#include <iostream>
#include <ctime>
#include <chrono>
#include <iomanip>

NS_BEGIN

Print::Print() {
}

Print::~Print() {
}

auto Print::help() -> std::list<std::string> {
  return {
    "Outputs passing objects to the standard output or a file",
    "format = If specified, can be 'raw' for outputing raw data as is, or 'tag' for tag only",
    "file = If specified, outputs to a file",
  };
}

void Print::config(const std::map<std::string, std::string> &params) {
  auto format = utils::get_param(params, "format", "default");
  if (format == "default") m_format = FormatDefault;
  else if (format == "raw") m_format = FormatRaw;
  else if (format == "tag") m_format = FormatTag;
  else throw std::runtime_error("unknown format");

  m_tag = utils::get_param(params, "tag", "");
  m_filename = utils::get_param(params, "file", "");
}

auto Print::clone() -> Module* {
  auto clone = new Print();
  clone->m_format = m_format;
  clone->m_tag = m_tag;
  clone->m_filename = m_filename;
  return clone;
}

void Print::pipe(
  std::shared_ptr<Context> ctx,
  std::unique_ptr<Object> obj,
  Object::Receiver out
) {
  std::ostream *s = &std::cout;
  if (!m_filename.empty()) {
    if (!m_tried_opening) {
      m_file.open(m_filename, std::ios_base::app | std::ios_base::binary);
      m_tried_opening = true;
    }
    if (m_file.is_open()) s = &m_file;
  }

  if (m_format == FormatRaw) {
    if (auto data = obj->as<Data>()) {
      for (auto chunk : data->chunks()) {
        auto data = std::get<0>(chunk);
        auto size = std::get<1>(chunk);
        s->write(data, size);
      }
      s->flush();
    }

  } else {
    auto &o = *s;

    static char s_hex[] = { "0123456789ABCDEF" };
    char time_str[100];
    auto time_now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch());
    std::time_t t = time_now.count() / 1000000;
    int ms = time_now.count() % 1000000;

    std::strftime(time_str, sizeof(time_str), "%a %b %d %H:%M:%S", std::localtime(&t));
    o << time_str << '.' << std::setfill('0') << std::setw(6) << ms << ' ';

    std::strftime(time_str, sizeof(time_str), "%Y", std::localtime(&t));
    o << time_str << ' ';

    if (m_format == FormatTag) {
      o << ctx->evaluate(m_tag);

    } else if (m_format == FormatDefault) {
      o << "[" << ctx->evaluate(m_tag) << "] ";
      o << obj->name();

      if (auto e = obj->as<SessionEnd>()) {
        o << " [" << e->error << "] " << e->message;
      } else if (auto k = obj->as<MapKey>()) {
        o << " [" << k->key << "]";
      } else if (auto v = obj->as<PrimitiveObject>()) {
        o << " [" << v->to_string() << "]";
      } else if (auto data = obj->as<Data>()) {
        o << " [" << data->size() << "]";
        if (!data->empty()) {
          std::string hex, txt;
          std::string hline(16*3+4+16, '-');
          o << std::endl << hline << std::endl;
          for (auto chunk : data->chunks()) {
            auto data = std::get<0>(chunk);
            auto size = std::get<1>(chunk);
            for (int i = 0; i < size; ++i) {
              auto ch = (unsigned char)data[i];
              hex += s_hex[ch >> 4];
              hex += s_hex[ch & 15];
              hex += ' ';
              txt += ch < 0x20 || ch == 0x7f ? '?' : ch;
              if (txt.length() == 16) {
                o << hex << " | " << txt << std::endl;
                hex.clear();
                txt.clear();
              }
            }
            if (!txt.empty()) {
              o << hex;
              for (int n = 16 - hex.length() / 3; n > 0; --n) o << " - ";
              o << " | ";
              o << txt;
              o << std::string(16 - txt.length(), '.');
              o << std::endl;
            }
          }
          o << hline;
        }
      }
    }

    o << std::endl;
    o << std::flush;
  }

  out(std::move(obj));
}

NS_END
