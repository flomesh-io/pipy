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

namespace pjs {

using namespace pipy;

template<> void ClassDef<Event>::init()
{
}

template<> void ClassDef<MessageStart>::init() {
  super<Event>();
  ctor([](Context &ctx) -> Object* {
    Object *context = nullptr, *head = nullptr;
    if (ctx.try_arguments(2, &context, &head)) {
      return MessageStart::make(context, head);
    } else if (ctx.arguments(0, &head)) {
      return MessageStart::make(head);
    } else {
      return nullptr;
    }
  });
  accessor("context", [](Object *obj, Value &val) { val.set(obj->as<MessageStart>()->context()); });
  accessor("head", [](Object *obj, Value &val) { val.set(obj->as<MessageStart>()->head()); });
}

template<> void ClassDef<MessageEnd>::init() {
  super<Event>();
  ctor([](Context &ctx) -> Object* { return MessageEnd::make(); });
}

template<> void EnumDef<SessionEnd::Error>::init() {
  define(SessionEnd::NO_ERROR           , "");
  define(SessionEnd::UNKNOWN_ERROR      , "UnknownError");
  define(SessionEnd::RUNTIME_ERROR      , "RuntimeError");
  define(SessionEnd::READ_ERROR         , "ReadError");
  define(SessionEnd::CANNOT_RESOLVE     , "CannotResolve");
  define(SessionEnd::CONNECTION_REFUSED , "ConnectionRefused");
  define(SessionEnd::UNAUTHORIZED       , "Unauthorized");
  define(SessionEnd::BUFFER_OVERFLOW    , "BufferOverflow");
}

template<> void ClassDef<SessionEnd>::init() {
  super<Event>();
  ctor([](Context &ctx) -> Object* { return SessionEnd::make(); });

  accessor("error", [](Object *obj, Value &val) {
    val.set(EnumDef<SessionEnd::Error>::name(obj->as<SessionEnd>()->error()));
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

template<> void ClassDef<Constructor<SessionEnd>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs