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
  if (target.is_array()) {
    create_chain(target.as<pjs::Array>());
  }
}

Pipe::Pipe(const Pipe &r)
  : Filter(r)
  , m_target(r.m_target)
  , m_target_map(r.m_target_map)
  , m_init_args(r.m_init_args)
  , m_chain(r.m_chain)
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
  if (!m_target.is_array()) {
    m_chain = nullptr;
  }
}

void Pipe::process(Event *evt) {
  if (!m_is_started) {
    if (!m_chain) {
      pjs::Value val;
      if (m_target.is_function()) {
        pjs::Value arg(evt);
        if (!Filter::callback(m_target.f(), 1, &arg, val)) return;
      } else {
        val = m_target;
      }

      if (!val.is_nullish()) {
        if (val.is_array()) {
          try {
            create_chain(val.as<pjs::Array>());
          } catch (std::runtime_error &err) {
            Filter::error("%s", err.what());
          }
        } else {
          auto pl = pipeline_layout(val);
          if (!pl) return;
          auto p = Pipeline::make(pl, Filter::context());
          auto q = Filter::pipeline();
          p->chain(q->chain(), q->chain_args());
          p->chain(Filter::output());
          m_pipeline = p;
        }
      }
    }

    if (m_chain) {
      auto p = Pipeline::make(m_chain->layout, context());
      p->chain(Filter::output());
      m_pipeline = p;
    }

    if (auto *p = m_pipeline.get()) {
      auto args = pjs::Value::empty;
      if (m_init_args) {
        if (m_init_args->is<pjs::Array>()) {
          args.set(m_init_args);
        } else if (m_init_args->is<pjs::Function>()) {
          pjs::Value arg(evt);
          if (!Filter::callback(m_init_args->as<pjs::Function>(), 1, &arg, args)) return;
        }
      }

      if (m_chain) {
        p->chain(m_chain->next, args);
      }

      p->start(args);
    }

    m_is_started = true;
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

  } else {
    Filter::output(evt);
  }
}

auto Pipe::pipeline_layout(const pjs::Value &val) -> PipelineLayout* {
  if (val.is<PipelineLayoutWrapper>()) {
    return val.as<PipelineLayoutWrapper>()->get();
  } else {
    pjs::Value v;
    auto s = val.to_string();
    if (m_target_map && m_target_map->get(s, v)) {
      if (v.is<PipelineLayoutWrapper>()) {
        s->release();
        return v.as<PipelineLayoutWrapper>()->get();
      } else {
        Filter::error("map entry '%s' is not a pipeline", s->c_str());
        s->release();
        return nullptr;
      }
    } else {
      Filter::error("pipeline '%s' not found", s->c_str());
      s->release();
      return nullptr;
    }
  }
}

void Pipe::create_chain(pjs::Array *array) {
  PipelineLayout::Chain *chain = nullptr;
  for (int i = 0; i < array->length(); i++) {
    pjs::Value v; array->get(i, v);
    auto p = pipeline_layout(v);
    if (!p) {
      delete chain;
      throw std::runtime_error(
        "cannot create pipeline array at index " + std::to_string(i)
      );
    }
    if (chain) {
      chain = chain->next = new PipelineLayout::Chain;
    } else {
      m_chain = chain = new PipelineLayout::Chain;
    }
    chain->layout = p;
  }
}

//
// PipeNext
//

PipeNext::PipeNext()
{
}

PipeNext::PipeNext(const PipeNext &r)
  : Filter(r)
{
}

PipeNext::~PipeNext()
{
}

void PipeNext::dump(Dump &d) {
  Filter::dump(d);
  d.name = "pipeNext";
}

auto PipeNext::clone() -> Filter* {
  return new PipeNext(*this);
}

void PipeNext::reset() {
  Filter::reset();
  m_next = nullptr;
}

void PipeNext::process(Event *evt) {
  if (auto *chain = Filter::pipeline()->chain()) {
    if (!m_next) {
      auto &a = Filter::pipeline()->chain_args();
      auto *p = Pipeline::make(chain->layout, context());
      p->chain(chain->next, a);
      p->chain(Filter::output());
      p->start(a);
      m_next = p;
    }
  }

  if (m_next) {
    Filter::output(evt, m_next->input());
  } else {
    Filter::output(evt);
  }
}

} // namespace pipy
