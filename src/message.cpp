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

#include "message.hpp"
#include "event.hpp"

namespace pipy {

//
// Message
//

thread_local Data::Producer Message::s_dp("Message");

bool Message::is_events(pjs::Object *obj) {
  if (!obj) return false;
  if (obj->is_instance_of<Event>()) return true;
  if (obj->is<Message>()) return true;
  if (obj->is<pjs::Array>()) {
    auto a = obj->as<pjs::Array>();
    for (int i = 0, n = a->length(); i < n; i++) {
      pjs::Value v;
      a->get(i, v);
      if (!v.is_instance_of<Event>() && !v.is<Message>()) return false;
    }
    return true;
  }
  return false;
}

bool Message::to_events(pjs::Object *obj, const std::function<bool(Event*)> &cb) {
  if (!obj) {
    return true;
  } else if (obj->is_instance_of<Event>()) {
    return cb(obj->as<Event>());
  } else if (obj->is_instance_of<Message>()) {
    auto msg = obj->as<Message>();
    pjs::Ref<MessageStart> start(MessageStart::make(msg->head()));
    pjs::Ref<MessageEnd> end(MessageEnd::make(msg->tail(), msg->payload()));
    if (!cb(start)) return false;
    if (auto body = msg->body()) if (!cb(body)) return false;
    if (!cb(end)) return false;
    return true;
  } else if (obj->is_array()) {
    auto *a = obj->as<pjs::Array>();
    bool ret = true;
    a->iterate_while([&](pjs::Value &v, int i) -> bool {
      if (v.is_null() || v.is_undefined()) {
        return true;
      } else if (v.is_instance_of(pjs::class_of<Event>())) {
        if (!cb(v.as<Event>())) return (ret = false);
        return true;
      } else if (v.is_instance_of(pjs::class_of<Message>())) {
        auto msg = obj->as<Message>();
        pjs::Ref<MessageStart> start(MessageStart::make(msg->head()));
        pjs::Ref<MessageEnd> end(MessageEnd::make(msg->tail(), msg->payload()));
        if (!cb(start)) return (ret = false);
        if (auto body = msg->body()) if (!cb(body)) return (ret = false);
        if (!cb(end)) return (ret = false);
        return true;
      } else {
        return (ret = false);
      }
    });
    return ret;
  } else {
    return false;
  }
}

bool Message::to_events(const pjs::Value &value, const std::function<bool(Event*)> &cb) {
  if (value.is_null() || value.is_undefined()) return true;
  if (value.is_function()) return false;
  if (value.is_object()) return to_events(value.o(), cb);
  return false;
}

auto Message::from(MessageStart *start, Data *body, MessageEnd *end) -> Message* {
  auto head = start ? start->head() : nullptr;
  return (end
    ? Message::make(head, body, end->tail(), end->payload())
    : Message::make(head, body));
}

bool Message::output(const pjs::Value &events, EventTarget::Input *input) {
  if (events.is_undefined()) {
    return true;
  } else if (events.is_object()) {
    return output(events.o(), input);
  } else {
    return false;
  }
}

bool Message::output(pjs::Object *events, EventTarget::Input *input) {
  if (!events) {
    return true;
  } else if (events->is_instance_of<Event>()) {
    input->input(events->as<Event>());
    return true;
  } else if (events->is_instance_of<Message>()) {
    events->as<Message>()->write(input);
    return true;
  } else if (events->is_array()) {
    auto *a = events->as<pjs::Array>();
    auto last = a->iterate_while([&](pjs::Value &v, int i) -> bool {
      if (v.is_instance_of(pjs::class_of<Event>())) {
        input->input(v.as<Event>());
        return true;
      } else if (v.is_instance_of(pjs::class_of<Message>())) {
        v.as<Message>()->write(input);
        return true;
      } else {
        return v.is_null() || v.is_undefined();
      }
    });
    return last == a->length();
  } else {
    return false;
  }
}

void Message::write(EventTarget::Input *input) {
  input->input(MessageStart::make(m_head));
  if (m_body && !m_body->empty()) input->input(m_body);
  input->input(MessageEnd::make(m_tail, m_payload));
}

//
// MessageReader
//

void MessageReader::reset() {
  m_start = nullptr;
  m_buffer.clear();
}

auto MessageReader::read(Event *evt) -> Message* {
  if (auto start = evt->as<MessageStart>()) {
    if (!m_start) {
      m_start = start;
    }
  } else if (auto data = evt->as<Data>()) {
    if (m_start) {
      m_buffer.push(*data);
    }
  } else if (evt->is_end()) {
    if (m_start) {
      auto head = m_start->head();
      auto body = Data::make(std::move(m_buffer));
      auto end = evt->as<MessageEnd>();
      auto msg = end ?
        Message::make(head, body, end->tail(), end->payload()) :
        Message::make(head, body);
      msg->retain();
      m_start = nullptr;
      return msg;
    }
  }
  return nullptr;
}

} // namespace pipy

namespace pjs {

template<> void ClassDef<pipy::Message>::init() {
  ctor([](Context &ctx) -> Object* {
    switch (ctx.argc()) {
      case 0: return pipy::Message::make();
      case 1: {
        const auto &arg0 = ctx.arg(0);
        if (arg0.is_string()) {
          return pipy::Message::make(arg0.s()->str());
        } else if (arg0.is_instance_of<pipy::Data>()) {
          return pipy::Message::make(arg0.as<pipy::Data>());
        } else if (arg0.is_object()) {
          return pipy::Message::make(arg0.o(), nullptr);
        } else {
          ctx.error_argument_type(0, "a string or an object");
          return nullptr;
        }
      }
      case 2: {
        Object *head = nullptr;
        if (!ctx.check(0, head, head)) return nullptr;
        const auto &arg1 = ctx.arg(1);
        if (arg1.is_string()) {
          return pipy::Message::make(head, arg1.s()->str());
        } else if (arg1.is_instance_of<pipy::Data>()) {
          return pipy::Message::make(head, arg1.as<pipy::Data>());
        } else if (arg1.is_null()) {
          return pipy::Message::make(head, nullptr);
        } else {
          return pipy::Message::make(head, nullptr, nullptr, arg1);
        }
      }
      case 3: {
        Object *head = nullptr;
        Object *tail = nullptr;
        if (!ctx.check(0, head, head)) return nullptr;
        if (!ctx.check(2, tail, tail)) return nullptr;
        const auto &arg1 = ctx.arg(1);
        if (arg1.is_string()) {
          return pipy::Message::make(head, arg1.s()->str(), tail);
        } else if (arg1.is_instance_of<pipy::Data>()) {
          return pipy::Message::make(head, arg1.as<pipy::Data>(), tail);
        } else if (arg1.is_null()) {
          return pipy::Message::make(head, nullptr, tail);
        } else {
          return pipy::Message::make(head, nullptr, tail, arg1);
        }
      }
      default: {
        ctx.error_argument_count(0, 3);
        return nullptr;
      }
    }
  });

  accessor("head", [](Object *obj, Value &ret) { ret.set(obj->as<pipy::Message>()->head()); });
  accessor("tail", [](Object *obj, Value &ret) { ret.set(obj->as<pipy::Message>()->tail()); });
  accessor("body", [](Object *obj, Value &ret) { ret.set(obj->as<pipy::Message>()->body()); });
  accessor("payload", [](Object *obj, Value &ret) { ret = obj->as<pipy::Message>()->payload(); });
}

template<> void ClassDef<Constructor<pipy::Message>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs
