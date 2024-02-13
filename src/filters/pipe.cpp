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

#include "pipe.hpp"
#include "api/pipeline.hpp"

namespace pipy {

//
// Pipe
//

Pipe::Pipe(
  const pjs::Value &target,
  pjs::Object *target_map,
  pjs::Object *init_args
) : m_target(target)
  , m_target_map(target_map)
  , m_init_args(init_args)
{
}

Pipe::Pipe(const Pipe &r)
  : Filter(r)
  , m_target(r.m_target)
  , m_target_map(r.m_target_map)
  , m_init_args(r.m_init_args)
  , m_buffer(r.m_buffer)
{
}

Pipe::~Pipe()
{
}

void Pipe::dump(Dump &d) {
  Filter::dump(d);
  d.name = "pipe";
}

auto Pipe::clone() -> Filter* {
  return new Pipe(*this);
}

void Pipe::reset() {
  Filter::reset();
  m_buffer.clear();
  m_pipeline = nullptr;
  m_is_started = false;
}

void Pipe::process(Event *evt) {
  if (!m_is_started) {
    pjs::Value val;
    if (m_target.is_function()) {
      pjs::Value arg(evt);
      if (!Filter::callback(m_target.f(), 1, &arg, val)) return;
    } else {
      val = m_target;
    }

    if (!val.is_nullish()) {
      PipelineLayoutWrapper *pl = nullptr;
      if (val.is<PipelineLayoutWrapper>()) {
        pl = val.as<PipelineLayoutWrapper>();
      } else {
        auto s = val.to_string();
        if (m_target_map && m_target_map->get(s, val)) {
          if (val.is<PipelineLayoutWrapper>()) {
            pl = val.as<PipelineLayoutWrapper>();
          }
        }
        if (!pl) {
          Filter::error("pipeline '%s' not found", s->c_str());
          s->release();
          return;
        }
        s->release();
      }

      auto p = pl->spawn(Filter::context());
      p->chain(Filter::output());
      m_pipeline = p;

      auto args = pjs::Value::empty;
      if (m_init_args) {
        if (m_init_args->is<pjs::Array>()) {
          args.set(m_init_args);
        } else if (m_init_args->is<pjs::Function>()) {
          pjs::Value arg(evt);
          if (!Filter::callback(m_init_args->as<pjs::Function>(), 1, &arg, args)) return;
        }
      }

      if (args.is_empty()) {
        p->start();
      } else if (args.is<pjs::Array>()) {
        auto a = args.as<pjs::Array>();
        auto d = a->elements();
        auto n = std::min((int)d->size(), a->length());
        p->start(n, d->elements());
      } else {
        p->start(1, &args);
      }

      m_is_started = true;
    }
  }

  if (!m_is_started) {
    m_buffer.push(evt);

  } else if (auto p = m_pipeline.get()) {
    auto i = p->input();
    if (!m_buffer.empty()) {
      m_buffer.flush(
        [&](Event *evt) {
          i->input(evt);
        }
      );
    }
    i->input(evt);
  }
}

} // namespace pipy
