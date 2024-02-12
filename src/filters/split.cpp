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

#include "split.hpp"

namespace pipy {

static std::string s_separator_too_long("separator over 1KB is not supported");

//
// Split
//

Split::Split(const pjs::Value &separator)
  : m_separator(separator)
{
  if (!separator.is_function()) {
    std::string str;
    if (separator.is<Data>()) {
      str = separator.as<Data>()->to_string();
    } else {
      auto s = separator.to_string();
      str = s->str();
      s->release();
    }
    if (str.length() > MAX_SEPARATOR) {
      throw std::runtime_error(s_separator_too_long);
    }
    m_kmp = new KMP(str.c_str(), str.length());
  }
}

Split::Split(const Split &r)
  : Filter(r)
  , m_separator(r.m_separator)
  , m_kmp(r.m_kmp)
{
}

Split::~Split()
{
}

void Split::dump(Dump &d) {
  Filter::dump(d);
  d.name = "split";
}

auto Split::clone() -> Filter* {
  return new Split(*this);
}

void Split::reset() {
  Filter::reset();
  delete m_split;
  m_split = nullptr;
  m_head = nullptr;
  m_started = false;
  if (m_separator.is_function()) {
    m_kmp = nullptr;
  }
}

void Split::process(Event *evt) {

  if (auto *start = evt->as<MessageStart>()) {
    if (!m_split) {
      m_head = start->head();
      if (!m_kmp) {
        pjs::Value ret;
        if (!eval(m_separator, ret)) return;
        if (ret.is<Data>()) {
          auto *d = ret.as<Data>();
          if (d->size() > MAX_SEPARATOR) {
            Filter::error("%s", s_separator_too_long.c_str());
            return;
          }
          uint8_t buf[MAX_SEPARATOR];
          d->to_bytes(buf);
          m_kmp = new KMP((char *)buf, d->size());
        } else {
          auto *s = ret.to_string();
          if (s->size() > MAX_SEPARATOR) {
            s->release();
            Filter::error("%s", s_separator_too_long.c_str());
            return;
          }
          m_kmp = new KMP(s->c_str(), s->size());
        }
      }
      m_split = m_kmp->split(
        [this](Data *data) {
          if (!m_started) {
            Filter::output(MessageStart::make(m_head));
            m_started = true;
          }
          if (data) {
            Filter::output(data);
          } else {
            Filter::output(MessageEnd::make());
            m_started = false;
          }
        }
      );
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_split) {
      m_split->input(*data);
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (m_split) {
      m_split->end();
      delete m_split;
      m_split = nullptr;
      m_head = nullptr;
      if (m_separator.is_function()) m_kmp = nullptr;
      if (evt->is<StreamEnd>()) Filter::output(evt);
    }
  }
}

} // namespace pipy
