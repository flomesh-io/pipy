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

#include "api/pipeline.hpp"
#include "worker.hpp"
#include "module.hpp"
#include "message.hpp"

// all filters
#include "filters/bgp.hpp"
#include "filters/branch.hpp"
#include "filters/chain.hpp"
#include "filters/connect.hpp"
#include "filters/compress.hpp"
#include "filters/decompress.hpp"
#include "filters/deframe.hpp"
#include "filters/demux.hpp"
#include "filters/deposit-message.hpp"
#include "filters/detect-protocol.hpp"
#include "filters/dubbo.hpp"
#include "filters/dummy.hpp"
#include "filters/dump.hpp"
#include "filters/exec.hpp"
#include "filters/fcgi.hpp"
#include "filters/fork.hpp"
#include "filters/http.hpp"
#include "filters/insert.hpp"
#include "filters/link.hpp"
#include "filters/link-async.hpp"
#include "filters/loop.hpp"
#include "filters/mime.hpp"
#include "filters/mqtt.hpp"
#include "filters/mux.hpp"
#include "filters/netlink.hpp"
#include "filters/on-body.hpp"
#include "filters/on-event.hpp"
#include "filters/on-message.hpp"
#include "filters/on-start.hpp"
#include "filters/pack.hpp"
#include "filters/print.hpp"
#include "filters/produce.hpp"
#include "filters/proxy-protocol.hpp"
#include "filters/read.hpp"
#include "filters/replace-body.hpp"
#include "filters/replace-event.hpp"
#include "filters/replace-message.hpp"
#include "filters/replace-start.hpp"
#include "filters/replay.hpp"
#include "filters/resp.hpp"
#include "filters/socks.hpp"
#include "filters/split.hpp"
#include "filters/tee.hpp"
#include "filters/thrift.hpp"
#include "filters/throttle.hpp"
#include "filters/tls.hpp"
#include "filters/use.hpp"
#include "filters/wait.hpp"
#include "filters/websocket.hpp"

namespace pipy {

//
// PipelineDesigner
//

auto PipelineDesigner::make_pipeline_layout(pjs::Context &ctx, pjs::Function *builder) -> PipelineLayout* {
  auto m = ctx.call_site().module;
  if (!m) throw std::runtime_error("cannot create pipeline layout without a module");
  auto pl = PipelineLayout::make(static_cast<JSModule*>(m));
  auto pd = PipelineDesigner::make(pl);
  pjs::Value arg(pd), ret;
  (*builder)(ctx, 1, &arg, ret);
  pd->close();
  if (ctx.ok()) return pl;
  pl->retain();
  pl->release();
  return nullptr;
}

auto PipelineDesigner::trace_location(pjs::Context &ctx) -> PipelineDesigner* {
  if (auto caller = ctx.caller()) {
    m_current_location = caller->call_site();
  }
  return this;
}

void PipelineDesigner::on_start(pjs::Object *starting_events) {
  if (!m_layout) throw std::runtime_error("pipeline layout is already built");
  if (m_current_filter) throw std::runtime_error("onStart() is only allowed prior to filters");
  if (m_has_on_start) throw std::runtime_error("duplicated onStart()");
  m_layout->on_start(starting_events);
  m_layout->on_start_location(m_current_location);
  m_has_on_start = true;
}

void PipelineDesigner::on_end(pjs::Function *handler) {
  if (!m_layout) throw std::runtime_error("pipeline layout is already built");
  if (m_current_filter) throw std::runtime_error("onEnd() is only allowed prior to filters");
  if (m_has_on_end) throw std::runtime_error("duplicated onEnd()");
  m_layout->on_end(handler);
  m_has_on_end = true;
}

void PipelineDesigner::to(pjs::Str *name) {
  if (!m_current_joint_filter) {
    throw std::runtime_error("calling to() without a joint-filter");
  }
  m_current_joint_filter->add_sub_pipeline(name);
  m_current_joint_filter = nullptr;
}

void PipelineDesigner::to(PipelineLayout *layout) {
  if (!m_current_joint_filter) {
    throw std::runtime_error("calling to() without a joint-filter");
  }
  m_current_joint_filter->add_sub_pipeline(layout);
  m_current_joint_filter = nullptr;
}

void PipelineDesigner::connect(const pjs::Value &target, pjs::Object *options) {
  if (options && options->is_function()) {
    append_filter(new Connect(target, options->as<pjs::Function>()));
  } else {
    append_filter(new Connect(target, options));
  }
}

void PipelineDesigner::demux_http(pjs::Object *options) {
  require_sub_pipeline(append_filter(new http::Demux(options)));
}

void PipelineDesigner::dummy() {
  append_filter(new Dummy());
}

void PipelineDesigner::dump(const pjs::Value &tag) {
  append_filter(new Dump(tag));
}

void PipelineDesigner::link(pjs::Str *name) {
  require_sub_pipeline(append_filter(new Link()));
  to(name);
}

void PipelineDesigner::link(pjs::Function *func) {
  append_filter(new Link(func));
}

void PipelineDesigner::mux_http(pjs::Function *session_selector, pjs::Object *options) {
  if (options && options->is_function()) {
    require_sub_pipeline(append_filter(new http::Mux(session_selector, options->as<pjs::Function>())));
  } else {
    require_sub_pipeline(append_filter(new http::Mux(session_selector, options)));
  }
}

void PipelineDesigner::close() {
  delete m_current_filter;
  m_current_filter = nullptr;
  m_current_joint_filter = nullptr;
  m_layout = nullptr;
}

void PipelineDesigner::check_integrity() {
  if (m_current_joint_filter) {
    throw std::runtime_error("missing .to(...) for the last filter");
  }
}

auto PipelineDesigner::append_filter(Filter *filter) -> Filter* {
  if (!m_layout) {
    delete filter;
    throw std::runtime_error("pipeline layout is already built");
  }
  check_integrity();
  filter->set_location(m_current_location);
  m_layout->append(filter);
  m_current_filter = filter;
  return filter;
}

void PipelineDesigner::require_sub_pipeline(Filter *filter) {
  m_current_joint_filter = filter;
}

//
// PipelineProducer
//

auto PipelineProducer::start(int argc, pjs::Value *argv) -> Pipeline* {
  auto worker = Worker::current();
  auto ctx = worker->new_runtime_context();
  auto p = Pipeline::make(m_layout, ctx);
  return p->start(argc, argv);
}

//
// PipelineProducer::Constructor
//

void PipelineProducer::Constructor::operator()(pjs::Context &ctx, pjs::Object *obj, pjs::Value &ret) {
  pjs::Function *f;
  if (!ctx.arguments(1, &f)) return;
  try {
    auto pl = PipelineDesigner::make_pipeline_layout(ctx, f);
    if (!pl) return;
    ret.set(PipelineProducer::make(pl));
  } catch (std::runtime_error &err) {
    ctx.error(err);
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<PipelineDesigner>::init() {

  // PipelineDesigner.onStart
  method("onStart", [](Context &ctx, Object *thiz, Value &result) {
    auto config = thiz->as<PipelineDesigner>()->trace_location(ctx);
    try {
      Object *starting_events = nullptr;
      if (!ctx.arguments(1, &starting_events)) return;
      if (!starting_events || (
          !starting_events->is<Function>() &&
          !Message::is_events(starting_events)
      )) {
        ctx.error_argument_type(0, "an Event, a Message, a function or an array");
        return;
      }
      config->on_start(starting_events);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // PipelineDesigner.onEnd
  method("onEnd", [](Context &ctx, Object *thiz, Value &result) {
    auto config = thiz->as<PipelineDesigner>()->trace_location(ctx);
    try {
      Function *handler;
      if (!ctx.arguments(1, &handler)) return;
      config->on_end(handler);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // PipelineDesigner.to
  method("to", [](Context &ctx, Object *thiz, Value &result) {
    try {
      pjs::Str *name;
      pjs::Function *func;
      if (ctx.get(0, name)) {
        thiz->as<PipelineDesigner>()->to(name);
      } else if (ctx.get(0, func) && func) {
        if (auto pl = PipelineDesigner::make_pipeline_layout(ctx, func)) {
          thiz->as<PipelineDesigner>()->to(pl);
        }
      } else {
        ctx.error_argument_type(0, "a string or a function");
        return;
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // PipelineDesigner.connect
  method("connect", [](Context &ctx, Object *thiz, Value &result) {
    auto config = thiz->as<PipelineDesigner>()->trace_location(ctx);
    Value target;
    Object *options = nullptr;
    if (!ctx.arguments(1, &target, &options)) return;
    try {
      config->connect(target, options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // PipelineDesigner.demuxHTTP
  method("demuxHTTP", [](Context &ctx, Object *thiz, Value &result) {
    auto config = thiz->as<PipelineDesigner>()->trace_location(ctx);
    try {
      Object *options = nullptr;
      if (!ctx.arguments(0, &options)) return;
      config->demux_http(options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // PipelineDesigner.dummy
  method("dummy", [](Context &ctx, Object *thiz, Value &result) {
    auto config = thiz->as<PipelineDesigner>()->trace_location(ctx);
    try {
      config->dummy();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // PipelineDesigner.dump
  method("dump", [](Context &ctx, Object *thiz, Value &result) {
    auto config = thiz->as<PipelineDesigner>()->trace_location(ctx);
    Value tag;
    if (!ctx.arguments(0, &tag)) return;
    try {
      config->dump(tag);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // PipelineDesigner.link
  method("link", [](Context &ctx, Object *thiz, Value &result) {
    auto config = thiz->as<PipelineDesigner>()->trace_location(ctx);
    try {
      Str *name;
      Function *func;
      if (ctx.get(0, name)) {
        config->link(name);
      } else if (ctx.get(0, func) && func) {
        config->link(func);
      } else {
        ctx.error_argument_type(0, "a string or a function");
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // PipelineDesigner.muxHTTP
  method("muxHTTP", [](Context &ctx, Object *thiz, Value &result) {
    auto config = thiz->as<PipelineDesigner>()->trace_location(ctx);
    try {
      Function *session_selector = nullptr;
      Object *options = nullptr;
      if (
        ctx.try_arguments(0, &session_selector, &options) ||
        ctx.try_arguments(0, &options)
      ) {
        config->mux_http(session_selector, options);
      } else {
        ctx.error_argument_type(0, "a function or an object");
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

template<> void ClassDef<PipelineProducer>::init() {
  method("start", [](Context &ctx, Object *thiz, Value &result) {
    thiz->as<PipelineProducer>()->start(ctx.argc(), ctx.argv());
  });
}

template<> void ClassDef<PipelineProducer::Constructor>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs
