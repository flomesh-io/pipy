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

#include "resp.hpp"

namespace pipy {
namespace resp {

static Data::Producer s_dp("RESP");

//
// Decoder
//

Decoder::Decoder()
{
}

Decoder::Decoder(const Decoder &r)
  : Filter(r)
{
}

Decoder::~Decoder()
{
}

void Decoder::dump(Dump &d) {
  Filter::dump(d);
  d.name = "decodeRESP";
}

auto Decoder::clone() -> Filter* {
  return new Decoder(*this);
}

void Decoder::reset() {
  Filter::reset();
  Deframer::reset();
  while (auto *s = m_stack) {
    m_stack = s->back;
    delete s;
  }
}

void Decoder::process(Event *evt) {
  if (evt->is<StreamEnd>()) {
    Filter::output(evt);
    Deframer::reset();
  } else if (auto *data = evt->as<Data>()) {
    Deframer::deframe(*data);
  }
}

auto Decoder::on_state(int state, int c) -> int {
  switch (state) {
    case START:
      switch (c) {
        case '+': m_read_data.clear(); return SIMPLE_STRING;
        case '-': m_read_data.clear(); return ERROR_STRING;
        case '$': m_read_int = 0; return BULK_STRING_SIZE;
        case ':': m_read_int = 0; return INTEGER_START;
        case '*': m_read_int = 0; return ARRAY_SIZE;
        default: return ERROR;
      }
      break;
    case NEWLINE:
      if (c == '\n') {
        return START;
      } else {
        return ERROR;
      }
    case SIMPLE_STRING:
      if (c == '\r') {
        auto s = m_read_data.to_string();
        push_value(pjs::Str::make(std::move(s)));
        return NEWLINE;
      } else {
        m_read_data.push(char(c), &s_dp);
        return SIMPLE_STRING;
      }
    case ERROR_STRING:
      if (c == '\r') {
        auto s = m_read_data.to_string();
        push_value(pjs::Error::make(pjs::Str::make(std::move(s))));
        return NEWLINE;
      } else {
        m_read_data.push(char(c), &s_dp);
        return ERROR_STRING;
      }
    case BULK_STRING_SIZE:
      if (c == '\r') {
        return BULK_STRING_SIZE_NEWLINE;
      } else if ('0' <= c && c <= '9') {
        m_read_int = m_read_int * 10 + (c - '0');
        return BULK_STRING_SIZE;
      } else {
        return ERROR;
      }
    case BULK_STRING_SIZE_NEWLINE:
      if (c == '\n') {
        m_read_data.clear();
        Deframer::read(m_read_int, &m_read_data);
        return BULK_STRING_DATA;
      } else {
        return ERROR;
      }
    case BULK_STRING_DATA:
      push_value(pjs::Str::make(m_read_data.to_string()));
      return BULK_STRING_DATA_CR;
    case BULK_STRING_DATA_CR:
      if (c == '\r') {
        return NEWLINE;
      } else {
        return ERROR;
      }
    case INTEGER_START:
      if (c == '-') {
        m_read_int = 0;
        return INTEGER_NEGATIVE;
      } else if ('0' <= c && c <= '9') {
        m_read_int = c - '0';
        return INTEGER_POSITIVE;
      } else {
        return ERROR;
      }
    case INTEGER_POSITIVE:
      if (c == '\r') {
        push_value((double)m_read_int);
        return NEWLINE;
      } else if ('0' <= c && c <= '9') {
        return INTEGER_POSITIVE;
      } else {
        return ERROR;
      }
    case INTEGER_NEGATIVE:
      if (c == '\r') {
        push_value(-(double)m_read_int);
        return NEWLINE;
      } else if ('0' <= c && c <= '9') {
        return INTEGER_NEGATIVE;
      } else {
        return ERROR;
      }
    case ARRAY_SIZE:
      if (c == '\r') {
        push_array(m_read_int);
        return NEWLINE;
      } else if ('0' <= c && c <= '9') {
        m_read_int = m_read_int * 10 + (c - '0');
        return ARRAY_SIZE;
      } else {
        return ERROR;
      }
      break;
    default: break;
  }
  return ERROR;
}

void Decoder::push_array(int size) {
}

void Decoder::push_value(const pjs::Value &value) {
}

void Decoder::pop_array() {
}

void Decoder::message_end() {
}

} // namespace resp
} // namespace pipy
