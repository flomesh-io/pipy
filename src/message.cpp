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
        if (!ctx.arguments(1, &head)) return nullptr;
        const auto &arg1 = ctx.arg(1);
        if (arg1.is_string()) {
          return pipy::Message::make(head, arg1.s()->str());
        } else if (arg1.is_instance_of<pipy::Data>()) {
          return pipy::Message::make(head, arg1.as<pipy::Data>());
        } else if (arg1.is_null()) {
          return pipy::Message::make(head, nullptr);
        } else {
          ctx.error_argument_type(1, "a string or a Data object");
          return nullptr;
        }
      }
      default: {
        ctx.error_argument_count(0, 2);
        return nullptr;
      }
    }
  });

  accessor("context", [](Object *obj, Value &ret) { ret.set(obj->as<pipy::Message>()->context()); });
  accessor("head", [](Object *obj, Value &ret) { ret.set(obj->as<pipy::Message>()->head()); });
  accessor("body", [](Object *obj, Value &ret) { ret.set(obj->as<pipy::Message>()->body()); });
}

template<> void ClassDef<Constructor<pipy::Message>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs