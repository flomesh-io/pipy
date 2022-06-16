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

#include "deframe.hpp"
#include "context.hpp"
#include "log.hpp"

namespace pipy {

Deframe::Deframe(pjs::Object *states)
  : m_states(states)
  , m_state_map(pjs::Object::make())
  , m_state_array(pjs::Array::make())
{
  if (!states) throw std::runtime_error("states cannot be null");
  m_states->iterate_all(
    [this](pjs::Str *k, pjs::Value &v) {
      m_state_map->set(k, m_state_array->length());
      m_state_array->push(v);
    }
  );
}

Deframe::Deframe(const Deframe &r)
  : m_states(r.m_states)
  , m_state_map(r.m_state_map)
  , m_state_array(r.m_state_array)
{
}

Deframe::~Deframe()
{
}

void Deframe::dump(Dump &d) {
  Filter::dump(d);
  d.name = "deframe";
}

auto Deframe::clone() -> Filter* {
  return new Deframe(*this);
}

void Deframe::reset() {
  Filter::reset();
  Deframer::reset();
}

void Deframe::process(Event *evt) {
  if (evt->is<StreamEnd>()) {
    output(evt);
    Deframer::reset();
  } else if (auto *data = evt->as<Data>()) {
    Deframer::deframe(*data);
  }
}

auto Deframe::on_state(int state, int c) -> int {
  if (state < 0 || state >= m_state_array->length()) return -1;

  pjs::Value v;
  m_state_array->get(state, v);

  if (v.is_function()) {
    auto *f = v.f();
    pjs::Value arg;
    if (m_read_buffer) {
      arg.set(m_read_buffer);
      m_read_buffer = nullptr;
    } else {
      arg.set(c);
    }
    auto &ctx = *context();
    (*f)(ctx, 1, &arg, v);
    if (!ctx.ok()) return -1;
  }

  if (v.is_array()) {
    auto a = v.as<pjs::Array>();
    auto i = 0;
    for (;;) {
      pjs::Value e;
      a->get(i, e);
      if (!e.is_instance_of<Event>()) break;
      Filter::output(e.as<Event>());
      i++;
    }

    pjs::Value state, size, buf;
    a->get(i+0, state);
    a->get(i+1, size);
    a->get(i+2, buf);

    auto n = 0;
    if (!size.is_nullish()) {
      n = int(size.to_number());
      if (n <= 0) {
        Log::error("[deframe] invalid reading length %d", n);
        return -1;
      }

      if (buf.is<Data>()) {
        m_read_buffer = buf.o();
        Deframer::read(n, buf.as<Data>());
      } else if (buf.is_array()) {
        m_read_buffer = buf.o();
        Deframer::read(n, buf.as<pjs::Array>());
      } else if (buf.is_nullish()) {
        Deframer::pass(n);
      } else {
        Log::error("[deframe] invalid read buffer");
        return -1;
      }
    }

    v = state;
  }

  if (v.is_string()) {
    pjs::Value id;
    m_state_map->get(v.s(), id);
    if (id.is_number()) {
      return id.n();
    } else {
      Log::error("[deframe] invalid state: %s", v.s()->c_str());
      return -1;
    }
  } else {
    Log::error("[deframe] invalid state returned");
    return -1;
  }
}

void Deframe::on_pass(const Data &data) {
  Filter::output(Data::make(data));
}

} // namespace pipy
