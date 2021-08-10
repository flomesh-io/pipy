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

#include "session.hpp"
#include "pipeline.hpp"
#include "module.hpp"
#include "filter.hpp"

namespace pipy {

//
// ReusableSession
//

ReusableSession::ReusableSession(Pipeline *pipeline)
  : m_pipeline(pipeline)
{
  Event::Receiver output = [this](Event *inp) {
    pjs::Ref<Event> ref(inp);
    if (m_output) m_output(inp);
    if (inp->is<SessionEnd>()) m_done = true;
  };

  Filter *last_filter = nullptr;
  const auto &filters = pipeline->filters();
  for (auto i = filters.rbegin(); i != filters.rend(); ++i) {
    auto filter = (*i)->clone();
    filter->m_pipeline = m_pipeline;
    filter->m_reusable_session = this;
    filter->m_next = last_filter;
    filter->m_output = output;
    filter->reset();
    output = [=](Event *inp) {
      pjs::Ref<Event> ref(inp);
      if (!m_context->ok()) return;
      enter_processing();
      filter->process(m_context, inp);
      leave_processing();
    };
    last_filter = filter;
  }

  m_filters = last_filter;
}

ReusableSession::~ReusableSession() {
  auto p = m_filters;
  while (p) {
    auto f = p;
    p = p->m_next;
    delete f;
  }
}

void ReusableSession::input(Event *evt) {
  enter_processing();

  pjs::Ref<Event> e(evt);
  if (auto f = m_filters) {
    f->process(m_context, evt);
  } else {
    if (m_output) m_output(evt);
    if (evt->is<SessionEnd>()) m_done = true;
  }

  leave_processing();
}

void ReusableSession::input(Message *msg) {
  if (!m_freed) input(MessageStart::make(msg->context(), msg->head()));
  if (!m_freed) input(msg->body());
  if (!m_freed) input(MessageEnd::make());
}

void ReusableSession::abort() {
  if (m_output) m_output(SessionEnd::make(SessionEnd::RUNTIME_ERROR));
  m_done = true;
}

void ReusableSession::free() {
  if (m_processing_level > 0) {
    m_freed = true;
  } else {
    reset();
    m_pipeline->free(this);
  }
}

void ReusableSession::reset() {
  for (auto *f = m_filters; f; f = f->m_next) f->reset();
  m_context = nullptr;
  m_output = nullptr;
  m_done = false;
  m_freed = false;
}

} // namespace pipy

//
// Session
//

namespace pjs {

using namespace pipy;

template<> void ClassDef<Session>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *filename, *pipeline;
    if (!ctx.arguments(2, &filename, &pipeline)) return nullptr;
    auto root = static_cast<pipy::Context*>(ctx.root());
    auto mod = root->worker()->get_module(filename);
    if (!mod) {
      std::string msg("module not found: ");
      ctx.error(msg + filename->str());
      return nullptr;
    }
    auto p = mod->find_named_pipeline(pipeline);
    if (!p) {
      std::string msg("pipeline not found: ");
      ctx.error(msg + pipeline->str());
      return nullptr;
    }
    return Session::make(root, p);
  });

  accessor("done", [](Object *obj, Value &ret) { ret.set(obj->as<Session>()->done()); });
  accessor("remoteAddress", [](Object *obj, Value &ret) { ret.set(obj->as<Session>()->remote_address()); });
  accessor("remotePort", [](Object *obj, Value &ret) { ret.set(obj->as<Session>()->remote_port()); });
  accessor("localAddress", [](Object *obj, Value &ret) { ret.set(obj->as<Session>()->local_address()); });
  accessor("localPort", [](Object *obj, Value &ret) { ret.set(obj->as<Session>()->local_port()); });

  method("input", [](Context &ctx, Object *obj, Value &ret) {
    auto *session = obj->as<Session>();
    auto *evt_class = class_of<Event>();
    auto *msg_class = class_of<Message>();
    for (int i = 0, n = ctx.argc(); i < n; i++) {
      auto &arg = ctx.arg(i);
      if (arg.is_array()) {
        auto *a = arg.as<Array>();
        auto last = a->iterate_while([=](Value &v, int) {
          if (v.is_instance_of(evt_class)) {
            session->input(v.as<Event>());
            return true;
          } else if (v.is_instance_of(msg_class)) {
            session->input(v.as<Message>());
            return true;
          } else {
            return false;
          }
        });
        if (last < n) {
          ctx.error("not an Event object");
          break;
        }
      } else if (arg.is_instance_of(evt_class)) {
        session->input(arg.as<Event>());
      } else if (arg.is_instance_of(msg_class)) {
        session->input(arg.as<Message>());
      } else {
        ctx.error("not an Event object");
        break;
      }
    }
  });
}

template<> void ClassDef<Constructor<Session>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs