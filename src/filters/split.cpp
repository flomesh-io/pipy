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

Split::Split(Data *separator) {
  if (separator->size() > MAX_SEPARATOR) {
    throw std::runtime_error(s_separator_too_long);
  }
  auto len = separator->size();
  char buf[len];
  separator->to_bytes((uint8_t*)buf);
  m_kmp = new KMP(buf, len);
}

Split::Split(pjs::Str *separator) {
  if (separator->size() > MAX_SEPARATOR) {
    throw std::runtime_error(s_separator_too_long);
  }
  m_kmp = new KMP(separator->c_str(), separator->size());
}

Split::Split(pjs::Function *callback)
  : m_callback(callback)
{
}

Split::Split(const Split &r)
  : Filter(r)
  , m_kmp(r.m_kmp)
  , m_callback(r.m_callback)
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
  if (m_callback) m_kmp = nullptr;
}

void Split::process(Event *evt) {

  if (auto *start = evt->as<MessageStart>()) {
    if (!m_split) {
      m_head = start->head();
      if (!m_kmp) {
        pjs::Value ret;
        if (!eval(m_callback, ret)) return;
        if (ret.is_string()) {
          auto *s = ret.s();
          if (s->size() > MAX_SEPARATOR) {
            Filter::error("%s", s_separator_too_long.c_str());
            return;
          }
          m_kmp = new KMP(s->c_str(), s->size());
        } else if (ret.is<Data>()) {
          auto *d = ret.as<Data>();
          if (d->size() > MAX_SEPARATOR) {
            Filter::error("%s", s_separator_too_long.c_str());
            return;
          }
          auto len = d->size();
          char buf[len];
          d->to_bytes((uint8_t*)buf);
          m_kmp = new KMP(buf, len);
        } else {
          Filter::error("callback did not return a string or a Data");
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
      if (m_callback) m_kmp = nullptr;
      if (evt->is<StreamEnd>()) Filter::output(evt);
    }
  }
}

} // namespace pipy
