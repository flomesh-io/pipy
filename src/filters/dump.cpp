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
#include "worker-thread.hpp"
#include "api/console.hpp"
#include "log.hpp"

#include <iostream>
#include <cstdarg>
#include <ctime>
#include <sstream>

namespace pipy {

thread_local static Data::Producer s_dp("dump()");

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

void Dump::dump(Filter::Dump &d) {
  Filter::dump(d);
  d.name = "dump";
}

auto Dump::clone() -> Filter* {
  return new Dump(*this);
}

void Dump::process(Event *evt) {
  static const char s_hex[] = { "0123456789ABCDEF" };
  static const std::string s_prefix_worker("[dump] [worker=");
  static const std::string s_prefix_context("] [context=");
  static const std::string s_prefix_head(", head = ");
  static const std::string s_prefix_tail(", tail = ");
  static const std::string s_prefix_payload(", payload = ");
  static const std::string s_hline(16*3+4+16, '-');
  static const std::string s_ellipsis("...");

  pjs::Value tag;
  if (!eval(m_tag, tag)) {
    output(evt);
    return;
  }

  Data buf;
  Data::Builder db(buf, &s_dp);

  char str[1000];
  auto str_fmt = [&](const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    auto n = std::vsnprintf(str, sizeof(str), fmt, ap);
    va_end(ap);
    return n;
  };

  db.push(str, Log::format_header(Log::INFO, str, sizeof(str)));
  db.push(s_prefix_worker);
  db.push(str, str_fmt( "%d", WorkerThread::current()->index()));
  db.push(s_prefix_context);
  db.push(str, str_fmt( "%d", context()->id()));
  db.push(']');
  db.push(' ');

  if (m_tag.to_boolean()) {
    auto s = tag.to_string();
    db.push('[');
    db.push(s->str());
    db.push(']');
    db.push(' ');
    s->release();
  }

  db.push(evt->name());

  if (auto start = evt->as<MessageStart>()) {
    if (auto head = start->head()) {
      db.push(s_prefix_head);
      Console::dump(head, db);
    }

  } else if (auto end = evt->as<MessageEnd>()) {
    if (auto tail = end->tail()) {
      db.push(s_prefix_tail);
      Console::dump(tail, db);
    }
    const auto &payload = end->payload();
    if (!payload.is_undefined()) {
      db.push(s_prefix_payload);
      Console::dump(payload, db);
    }

  } else if (auto end = evt->as<StreamEnd>()) {
    if (end->error() != StreamEnd::NO_ERROR) {
      db.push(' ');
      db.push('[');
      db.push(end->message());
      db.push(']');
      db.push(' ');
    }

  } else if (auto data = evt->as<Data>()) {
    db.push(' ');
    db.push('[');
    db.push(str, str_fmt("%d", data->size()));
    db.push(']');
    if (!data->empty()) {
      if (Log::is_enabled(Log::DUMP)) {
        char hex[100], txt[100];
        auto i = 0, j = 0;
        db.push('\n');
        db.push(s_hline);
        db.push('\n');
        data->scan(
          [&](int c) {
            auto ch = uint8_t(c);
            hex[i++] = s_hex[ch >> 4];
            hex[i++] = s_hex[ch & 15];
            hex[i++] = ' ';
            txt[j++] = ch < 0x20 || ch >= 0x7f ? '?' : ch;
            if (j == 16) {
              hex[i++] = ' ';
              hex[i++] = '|';
              hex[i++] = ' ';
              db.push(hex, i);
              db.push(txt, j);
              db.push('\n');
              i = 0; j = 0;
            }
            return true;
          }
        );
        if (j > 0) {
          for (int n = 16 - j; n > 0; --n) {
            hex[i++] = ' ';
            hex[i++] = '-';
            hex[i++] = ' ';
            txt[j++] = '.';
          }
          hex[i++] = ' ';
          hex[i++] = '|';
          hex[i++] = ' ';
          db.push(hex, i);
          db.push(txt, j);
          db.push('\n');
        }
        db.push(s_hline);
      } else {
        db.push(' ');
        db.push('[');
        int i = 0;
        data->scan(
          [&](int c) {
            auto ch = uint8_t(c);
            db.push(' ');
            db.push(s_hex[ch >> 4]);
            db.push(s_hex[ch & 15]);
            return (++i < 16);
          }
        );
        db.push(' ');
        if (data->size() > 16) {
          db.push(s_ellipsis);
          db.push(' ');
        }
        db.push(']');
      }
    }
  }

  db.flush();
  Log::write(buf);

  output(evt);
}

} // namespace pipy
