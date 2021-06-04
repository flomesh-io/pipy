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
    Object *head;
    pipy::Data *body = nullptr;
    Str *body_str;
    if (ctx.try_arguments(0, &body)) {
      return pipy::Message::make(nullptr, body);
    } else if (ctx.try_arguments(1, &body_str)) {
      return pipy::Message::make(nullptr, body_str->str());
    } else if (ctx.try_arguments(2, &head, &body_str)) {
      return pipy::Message::make(head, body_str->str());
    } else if (ctx.try_arguments(1, &head, &body)) {
      return pipy::Message::make(head, body);
    } else {
      ctx.error_argument_type(0, "an object");
      return nullptr;
    }
  });

  accessor("head", [](Object *obj, Value &ret) { ret.set(obj->as<pipy::Message>()->head()); });
  accessor("body", [](Object *obj, Value &ret) { ret.set(obj->as<pipy::Message>()->body()); });
}

template<> void ClassDef<Constructor<pipy::Message>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs