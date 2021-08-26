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

#include "filter.hpp"
#include "session.hpp"
#include "pipeline.hpp"
#include "module.hpp"
#include "message.hpp"
#include "logging.hpp"

#include <sstream>

namespace pipy {

auto Filter::draw(std::list<std::string> &links, bool &fork) -> std::string {
  std::stringstream ss;
  dump(ss);
  return ss.str();
}

auto Filter::pipeline(pjs::Str *name) -> Pipeline* {
  auto mod = m_pipeline->module();
  if (auto p = mod->find_named_pipeline(name)) {
    return p;
  } else {
    std::string msg("unknown pipeline: ");
    msg += name->str();
    throw std::runtime_error(msg);
  }
}

auto Filter::new_context(Context *base) -> Context* {
  auto mod = m_pipeline->module();
  if (!mod) return new Context();
  return mod->worker()->new_runtime_context(base);
}

bool Filter::output(const pjs::Value &evt) {
  if (evt.is_instance_of(pjs::class_of<Event>())) {
    output(evt.as<Event>());
    return true;
  } else if (evt.is_instance_of(pjs::class_of<Message>())) {
    auto *msg = evt.as<Message>();
    auto *body = msg->body();
    output(MessageStart::make(msg->head()));
    if (body) output(body);
    output(MessageEnd::make());
    return true;
  } else if (evt.is_array()) {
    auto *a = evt.as<pjs::Array>();
    auto last = a->iterate_while([&](pjs::Value &v, int i) -> bool {
      if (v.is_instance_of(pjs::class_of<Event>())) {
        output(v.as<Event>());
        return true;
      } else if (v.is_instance_of(pjs::class_of<Message>())) {
        auto *msg = v.as<Message>();
        auto *body = msg->body();
        output(MessageStart::make(msg->head()));
        if (body) output(body);
        output(MessageEnd::make());
        return true;
      } else {
        return v.is_null() || v.is_undefined();
      }
    });
    if (last < a->length()) {
      Log::error("[filter] not an Event object");
      return false;
    }
    return true;
  } else if (evt.is_null() || evt.is_undefined()) {
    return true;
  } else {
    Log::error("[filter] not an Event object");
    return false;
  }
}

bool Filter::eval(Context &ctx, pjs::Value &param, pjs::Value &result) {
  if (param.is_function()) {
    auto f = param.as<pjs::Function>();
    ((*f)(ctx, 0, nullptr, result));
    if (ctx.ok()) return true;
    auto mod = m_reusable_session->pipeline()->module();
    Log::pjs_error(ctx.error(), mod->source());
    ctx.reset();
    abort();
    return false;
  } else {
    result = param;
    return true;
  }
}

bool Filter::callback(Context &ctx, pjs::Function *func, int argc, pjs::Value argv[], pjs::Value &result) {
  (*func)(ctx, argc, argv, result);
  if (ctx.ok()) return true;
  auto mod = m_reusable_session->pipeline()->module();
  Log::pjs_error(ctx.error(), mod->source());
  ctx.reset();
  abort();
  return false;
}

void Filter::abort() {
  m_reusable_session->abort();
}

} // namespace pipy