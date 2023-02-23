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

#include <cstdio>
#include <functional>

namespace pipy {

//
// RESP
//

thread_local static Data::Producer s_dp("RESP");

auto RESP::decode(const Data &data) -> pjs::Array* {
  pjs::Array *a = pjs::Array::make();
  StreamParser sp(
    [=](const pjs::Value &value) {
      a->push(value);
    }
  );
  Data buf(data);
  sp.parse(buf);
  return a;
}

void RESP::encode(const pjs::Value &value, Data &data) {
  Data::Builder db(data, &s_dp);
  encode(value, db);
  db.flush();
}

void RESP::encode(const pjs::Value &value, Data::Builder &db) {
  std::function<void(const pjs::Value &)> write_value;
  write_value = [&](const pjs::Value &value) {
    if (value.is_string()) {
      db.push('+');
      db.push(value.s()->str());
      db.push('\r');
      db.push('\n');

    } else if (value.is_number()) {
      char buf[100];
      db.push(buf, std::snprintf(buf, sizeof(buf), ":%lld\r\n", (long long)value.n()));

    } else if (value.is_array()) {
      auto *a = value.as<pjs::Array>();
      char buf[100];
      db.push(buf, std::snprintf(buf, sizeof(buf), "*%d\r\n", (int)a->length()));
      a->iterate_all(
        [&](pjs::Value &v, int) {
          write_value(v);
        }
      );

    } else if (value.is<Data>()) {
      auto *data = value.as<Data>();
      char buf[100];
      db.push(buf, std::snprintf(buf, sizeof(buf), "$%d\r\n", (int)data->size()));
      db.push(*data);
      db.push('\r');
      db.push('\n');

    } else if (value.is<pjs::Error>()) {
      db.push('-');
      db.push(value.as<pjs::Error>()->message()->str());
      db.push('\r');
      db.push('\n');

    } else if (value.is_nullish()) {
      db.push("$-1\r\n", 5);

    } else {
      auto *s = value.to_string();
      db.push('+');
      db.push(s->str());
      db.push('\r');
      db.push('\n');
      s->release();
    }
  };

  write_value(value);
  db.flush();
}

//
// RESP::Parser
//

RESP::Parser::Parser()
  : m_read_data(Data::make())
{
}

void RESP::Parser::reset() {
  Deframer::reset();
  Deframer::pass_all(true);
  while (auto *s = m_stack) {
    m_stack = s->back;
    delete s;
  }
  m_stack = nullptr;
  m_root = pjs::Value::undefined;
  m_read_data->clear();
}

auto RESP::Parser::on_state(int state, int c) -> int {
  switch (state) {
    case START:
      message_end();
      message_start();
      switch (c) {
        case '+': m_read_data->clear(); return SIMPLE_STRING;
        case '-': m_read_data->clear(); return ERROR_STRING;
        case '$': m_read_int = 0; return BULK_STRING_SIZE;
        case ':': m_read_int = 0; return INTEGER_START;
        case '*': m_read_int = 0; return ARRAY_SIZE;
        default: return ERROR;
      }
      break;
    case NEWLINE:
      if (c == '\n') {
        if (!m_stack) Deframer::need_flush();
        return START;
      } else {
        return ERROR;
      }
    case SIMPLE_STRING:
      if (c == '\r') {
        auto s = m_read_data->to_string();
        push_value(pjs::Str::make(std::move(s)));
        return NEWLINE;
      } else {
        m_read_data->push(char(c), &s_dp);
        return SIMPLE_STRING;
      }
    case ERROR_STRING:
      if (c == '\r') {
        auto s = m_read_data->to_string();
        push_value(pjs::Error::make(pjs::Str::make(std::move(s))));
        return NEWLINE;
      } else {
        m_read_data->push(char(c), &s_dp);
        return ERROR_STRING;
      }
    case BULK_STRING_SIZE:
      if (c == '\r') {
        return BULK_STRING_SIZE_NEWLINE;
      } else if (c == '-') {
        return BULK_STRING_SIZE_NEGATIVE;
      } else if ('0' <= c && c <= '9') {
        m_read_int = m_read_int * 10 + (c - '0');
        return BULK_STRING_SIZE;
      } else {
        return ERROR;
      }
    case BULK_STRING_SIZE_NEWLINE:
      if (c == '\n') {
        if (m_read_int > 0) {
          m_read_data->clear();
          Deframer::read(m_read_int, m_read_data);
          return BULK_STRING_DATA;
        } else {
          push_value(Data::make());
          return BULK_STRING_DATA_CR;
        }
      } else {
        return ERROR;
      }
    case BULK_STRING_SIZE_NEGATIVE:
      if (c == '1') {
        return BULK_STRING_SIZE_NEGATIVE_CR;
      } else {
        return ERROR;
      }
    case BULK_STRING_SIZE_NEGATIVE_CR:
      if (c == '\r') {
        push_value(pjs::Value::null);
        return NEWLINE;
      } else {
        return ERROR;
      }
    case BULK_STRING_DATA:
      push_value(Data::make(std::move(*m_read_data)));
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
        push_value(pjs::Array::make(m_read_int));
        return NEWLINE;
      } else if (c == '-') {
        return ARRAY_SIZE_NEGATIVE;
      } else if ('0' <= c && c <= '9') {
        m_read_int = m_read_int * 10 + (c - '0');
        return ARRAY_SIZE;
      } else {
        return ERROR;
      }
      break;
    case ARRAY_SIZE_NEGATIVE:
      if (c == '1') {
        return ARRAY_SIZE_NEGATIVE_CR;
      } else {
        return ERROR;
      }
    case ARRAY_SIZE_NEGATIVE_CR:
      if (c == '\r') {
        push_value(pjs::Value::null);
        return NEWLINE;
      } else {
        return ERROR;
      }
    default: break;
  }
  return ERROR;
}

void RESP::Parser::parse(Data &data) {
  Deframer::deframe(data);
  if (Deframer::state() == START) message_end();
}

void RESP::Parser::push_value(const pjs::Value &value) {
  if (auto *l = m_stack) {
    l->array->set(l->index++, value);
  } else {
    m_root = value;
  }
  if (value.is_array() && value.as<pjs::Array>()->length() > 0) {
    auto *l = new Level;
    l->back = m_stack;
    l->array = value.as<pjs::Array>();
    m_stack = l;
  } else {
    auto *l = m_stack;
    while (l && l->index == l->array->length()) {
      auto *level = l; l = l->back;
      delete level;
    }
    m_stack = l;
    if (!l) Deframer::need_flush();
  }
}

void RESP::Parser::message_start() {
  if (!m_stack && m_root.is_undefined()) {
    on_message_start();
  }
}

void RESP::Parser::message_end() {
  if (!m_stack && !m_root.is_undefined()) {
    on_message_end(m_root);
    m_root = pjs::Value::undefined;
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

//
// RESP
//

template<> void ClassDef<RESP>::init() {
  ctor();

  method("decode", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    if (!ctx.arguments(1, &data)) return;
    ret.set(RESP::decode(*data));
  });

  method("encode", [](Context &ctx, Object *obj, Value &ret) {
    Value val;
    if (!ctx.arguments(1, &val)) return;
    auto *data = pipy::Data::make();
    RESP::encode(val, *data);
    ret.set(data);
  });
}

} // namespace pjs
