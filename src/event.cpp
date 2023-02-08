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
#include "pipeline.hpp"

namespace pipy {

//
// Event
//

auto Event::name() const -> const char* {
  switch (m_type) {
  case Data: return "Data";
  case MessageStart: return "MessageStart";
  case MessageEnd: return "MessageEnd";
  case StreamEnd: return "StreamEnd";
  }
  return "???";
}

auto StreamEnd::message() const -> const char* {
  switch (m_error) {
    case NO_ERROR: return "no error";
    case REPLAY: return "replay";
    case UNKNOWN_ERROR: return "unknown error";
    case RUNTIME_ERROR: return "runtime error";
    case READ_ERROR: return "read error";
    case WRITE_ERROR: return "write error";
    case CANNOT_RESOLVE: return "cannot resolve";
    case CONNECTION_CANCELED: return "connection canceled";
    case CONNECTION_RESET: return "connection reset";
    case CONNECTION_REFUSED: return "connection refused";
    case CONNECTION_TIMEOUT: return "connection timeout";
    case READ_TIMEOUT: return "read timeout";
    case WRITE_TIMEOUT: return "write timeout";
    case IDLE_TIMEOUT: return "idle timeout";
    case UNAUTHORIZED: return "unauthorized";
    case BUFFER_OVERFLOW: return "buffer overflow";
  }
  return "???";
}

//
// EventTarget::Input
//

auto EventTarget::Input::dummy() -> Input* {
  thread_local static pjs::Ref<Input> dummy(new DummyInput());
  return dummy;
}

auto EventTarget::Input::make(Input *input) -> Input* {
  return new InputInput(input);
}

auto EventTarget::Input::make(EventTarget *target) -> Input* {
  return new TargetInput(target);
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Event>::init()
{
}

template<> void ClassDef<MessageStart>::init() {
  super<Event>();
  ctor([](Context &ctx) -> Object* {
    Object *head = nullptr;
    if (!ctx.arguments(0, &head)) return nullptr;
    return MessageStart::make(head);
  });
  accessor("head", [](Object *obj, Value &val) { val.set(obj->as<MessageStart>()->head()); });
}

template<> void ClassDef<MessageEnd>::init() {
  super<Event>();
  ctor([](Context &ctx) -> Object* {
    Object *tail = nullptr;
    Object *payload = nullptr;
    if (!ctx.arguments(0, &tail, &payload)) return nullptr;
    return MessageEnd::make(tail, payload);
  });
  accessor("tail", [](Object *obj, Value &val) { val.set(obj->as<MessageEnd>()->tail()); });
  accessor("payload", [](Object *obj, Value &val) { val = obj->as<MessageEnd>()->payload(); });
}

template<> void EnumDef<StreamEnd::Error>::init() {
  define(StreamEnd::NO_ERROR           , "");
  define(StreamEnd::REPLAY             , "Replay");
  define(StreamEnd::UNKNOWN_ERROR      , "UnknownError");
  define(StreamEnd::RUNTIME_ERROR      , "RuntimeError");
  define(StreamEnd::READ_ERROR         , "ReadError");
  define(StreamEnd::WRITE_ERROR        , "WriteError");
  define(StreamEnd::CANNOT_RESOLVE     , "CannotResolve");
  define(StreamEnd::CONNECTION_CANCELED, "ConnectionCanceled");
  define(StreamEnd::CONNECTION_RESET   , "ConnectionReset");
  define(StreamEnd::CONNECTION_REFUSED , "ConnectionRefused");
  define(StreamEnd::CONNECTION_TIMEOUT , "ConnectionTimeout");
  define(StreamEnd::READ_TIMEOUT       , "ReadTimeout");
  define(StreamEnd::WRITE_TIMEOUT      , "WriteTimeout");
  define(StreamEnd::IDLE_TIMEOUT       , "IdleTimeout");
  define(StreamEnd::UNAUTHORIZED       , "Unauthorized");
  define(StreamEnd::BUFFER_OVERFLOW    , "BufferOverflow");
}

template<> void ClassDef<StreamEnd>::init() {
  super<Event>();

  ctor([](Context &ctx) -> Object* {
    Str *error = nullptr;
    if (!ctx.arguments(0, &error)) return nullptr;
    StreamEnd::Error err = StreamEnd::NO_ERROR;
    if (error) {
      err = EnumDef<StreamEnd::Error>::value(error);
      if (int(err) < 0) {
        ctx.error("unknown error type");
        return nullptr;
      }
    }
    return StreamEnd::make(err);
  });

  accessor("error", [](Object *obj, Value &val) {
    val.set(EnumDef<StreamEnd::Error>::name(obj->as<StreamEnd>()->error()));
  });
}

template<> void ClassDef<Constructor<MessageStart>>::init() {
  super<Function>();
  ctor();
}

template<> void ClassDef<Constructor<MessageEnd>>::init() {
  super<Function>();
  ctor();
}

template<> void ClassDef<Constructor<StreamEnd>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs
