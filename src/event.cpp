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

#include "event.hpp"

#include "data.hpp"
#include "input.hpp"
#include "net.hpp"
#include "pipeline.hpp"

#ifdef _WIN32
#undef NO_ERROR
#endif

namespace pipy {

//
// SharedEvent
//

SharedEvent::SharedEvent(Event *evt)
    : m_type(evt ? evt->type() : (Event::Type)-1) {
  switch (m_type) {
    case Event::Type::Data:
      m_data = SharedData::make(*evt->as<Data>());
      break;
    case Event::Type::MessageStart:
      m_head_tail = pjs::SharedObject::make(evt->as<MessageStart>()->head());
      break;
    case Event::Type::MessageEnd:
      m_head_tail = pjs::SharedObject::make(evt->as<MessageEnd>()->tail());
      m_payload = evt->as<MessageEnd>()->payload();
      break;
    case Event::Type::StreamEnd:
      m_error_code = evt->as<StreamEnd>()->error_code();
      m_error = evt->as<StreamEnd>()->error();
      break;
    default:
      break;
  }
}

SharedEvent::~SharedEvent() {}

auto SharedEvent::to_event() -> Event * {
  switch (m_type) {
    case Event::Type::Data: {
      auto d = Data::make();
      m_data->to_data(*d);
      return d;
    }
    case Event::Type::MessageStart: {
      return MessageStart::make(m_head_tail ? m_head_tail->to_object()
                                            : nullptr);
    }
    case Event::Type::MessageEnd: {
      pjs::Value p;
      m_payload.to_value(p);
      return MessageEnd::make(m_head_tail ? m_head_tail->to_object() : nullptr,
                              p);
    }
    case Event::Type::StreamEnd: {
      if (m_error_code == StreamEnd::RUNTIME_ERROR) {
        pjs::Value e;
        m_error.to_value(e);
        return StreamEnd::make(e);
      } else {
        return StreamEnd::make(m_error_code);
      }
    }
    default:
      return nullptr;
  }
}

//
// EventTarget::Input
//

auto EventTarget::Input::dummy() -> Input * {
  thread_local static pjs::Ref<Input> dummy(new DummyInput());
  return dummy;
}

auto EventTarget::Input::make(Input *input) -> Input * {
  return new InputInput(input);
}

auto EventTarget::Input::make(EventTarget *target) -> Input * {
  return new TargetInput(target);
}

void EventTarget::Input::input_async(Event *evt) {
  retain();
  evt->retain();
  Net::current().post([=]() {
    InputContext ic;
    input(evt);
    release();
    evt->release();
  });
}

void EventTarget::Input::flush_async() {
  retain();
  Net::current().post([this]() {
    InputContext ic;
    input(Data::make());
    release();
  });
}

void EventTarget::Input::flush() { input(Data::make()); }

}  // namespace pipy

namespace pjs {

using namespace pipy;

//
// Event::Type
//

template <>
void EnumDef<Event::Type>::init() {
  define(Event::Type::Data, "Data");
  define(Event::Type::MessageStart, "MessageStart");
  define(Event::Type::MessageEnd, "MessageEnd");
  define(Event::Type::StreamEnd, "StreamEnd");
}

//
// StreamEnd::Error
//

template <>
void EnumDef<StreamEnd::Error>::init() {
  define(StreamEnd::REPLAY, "Replay");
  define(StreamEnd::RUNTIME_ERROR, "RuntimeError");
  define(StreamEnd::READ_ERROR, "ReadError");
  define(StreamEnd::WRITE_ERROR, "WriteError");
  define(StreamEnd::CANNOT_RESOLVE, "CannotResolve");
  define(StreamEnd::CONNECTION_ABORTED, "ConnectionAborted");
  define(StreamEnd::CONNECTION_RESET, "ConnectionReset");
  define(StreamEnd::CONNECTION_REFUSED, "ConnectionRefused");
  define(StreamEnd::CONNECTION_TIMEOUT, "ConnectionTimeout");
  define(StreamEnd::READ_TIMEOUT, "ReadTimeout");
  define(StreamEnd::WRITE_TIMEOUT, "WriteTimeout");
  define(StreamEnd::IDLE_TIMEOUT, "IdleTimeout");
  define(StreamEnd::BUFFER_OVERFLOW, "BufferOverflow");
  define(StreamEnd::PROTOCOL_ERROR, "ProtocolError");
  define(StreamEnd::UNAUTHORIZED, "Unauthorized");
}

//
// Event
//

template <>
void ClassDef<Event>::init() {
  accessor("type", [](Object *obj, Value &ret) {
    ret.set(EnumDef<Event::Type>::name(obj->as<Event>()->type()));
  });
}

//
// MessageStart
//

template <>
void ClassDef<MessageStart>::init() {
  super<Event>();
  ctor([](Context &ctx) -> Object * {
    Object *head = nullptr;
    if (!ctx.arguments(0, &head)) return nullptr;
    return MessageStart::make(head);
  });
  accessor("head", [](Object *obj, Value &val) {
    val.set(obj->as<MessageStart>()->head());
  });
}

//
// MessageEnd
//

template <>
void ClassDef<MessageEnd>::init() {
  super<Event>();
  ctor([](Context &ctx) -> Object * {
    Object *tail = nullptr;
    Value payload;
    if (!ctx.arguments(0, &tail, &payload)) return nullptr;
    return MessageEnd::make(tail, payload);
  });
  accessor("tail", [](Object *obj, Value &val) {
    val.set(obj->as<MessageEnd>()->tail());
  });
  accessor("payload", [](Object *obj, Value &val) {
    val = obj->as<MessageEnd>()->payload();
  });
}

//
// StreamEnd
//

template <>
void ClassDef<StreamEnd>::init() {
  super<Event>();
  ctor([](Context &ctx) -> Object * {
    EnumValue<StreamEnd::Error> error = StreamEnd::Error::NO_ERROR;
    if (ctx.get(0, error)) {
      return StreamEnd::make(error);
    } else if (!ctx.is_undefined(0)) {
      return StreamEnd::make(ctx.arg(0));
    } else {
      return StreamEnd::make();
    }
  });
  accessor("error", [](Object *obj, Value &val) {
    auto *se = obj->as<StreamEnd>();
    if (se->error().is_undefined() &&
        se->error_code() != StreamEnd::Error::NO_ERROR) {
      val.set(EnumDef<StreamEnd::Error>::name(se->error_code()));
    } else {
      val = se->error();
    }
  });
}

//
// Constructors
//

template <>
void ClassDef<Constructor<MessageStart>>::init() {
  super<Function>();
  ctor();
}

template <>
void ClassDef<Constructor<MessageEnd>>::init() {
  super<Function>();
  ctor();
}

template <>
void ClassDef<Constructor<StreamEnd>>::init() {
  super<Function>();
  ctor();
}

}  // namespace pjs
