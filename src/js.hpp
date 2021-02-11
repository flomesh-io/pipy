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

#ifndef JS_HPP
#define JS_HPP

#include "quickjs.h"

#include "context.hpp"
#include "data.hpp"
#include "pool.hpp"

NS_BEGIN

#define DECLARE_CLASS(T) \
  template<> JSClassID js::Class<T>::s_class_id; \
  template<> const char* js::Class<T>::s_class_name;

#define DEFINE_CLASS(T) \
  template<> JSClassID js::Class<T>::s_class_id = 0; \
  template<> const char* js::Class<T>::s_class_name = #T;

#define DEFINE_SYMBOLS(T) \
  template<> int js::Symbols<T>::s_symbols[int(T::__MAX__)] = { 0 };

#define DEFINE_SYMBOL(T, S) define(T::S, #S)

#define DEFINE_SYMBOL_NAME(T, S, N) define(T::S, N)

#define DEFINE_FUNC(C, F, N) define_func(C, #F, F, N)

#define DEFINE_GET(C, N) define_prop(C, #N, get_##N, nullptr)
#define DEFINE_SET(C, N) define_prop(C, #N, nullptr, set_##N)
#define DEFINE_GETSET(C, N) define_prop(C, #N, get_##N, set_##N)

namespace js {

  class Worker;
  class Program;
  class Session;
  class Context;
  class Event;
  class Buffer;

  //
  // Worker
  //

  class Worker {
  public:
    static auto current() -> Worker*;

    void set_root_path(const char *path) { m_root_path = path; }

    auto runtime() const -> JSRuntime* { return m_rt; }
    auto context() const -> JSContext* { return m_ctx; }

    auto new_symbol(const char *str) -> int {
      auto id = s_symbols.size();
      s_symbols.push_back(str);
      m_symbols.push_back(JS_NewAtom(m_ctx, str));
      return id;
    }

    auto get_symbol(int id) -> JSAtom {
      return m_symbols[id];
    }

  private:
    Worker();
    ~Worker();

    JSRuntime* m_rt;
    JSContext* m_ctx;
    std::string m_root_path;
    std::vector<JSAtom> m_symbols;

    static thread_local Worker* s_worker;
    static std::vector<std::string> s_symbols;

    static auto module_loader(JSContext *ctx, const char *module_name, void *opaque) -> JSModuleDef*;
    static auto console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) -> JSValue;
  };

  //
  // Program
  //

  class Program {
  public:
    Program(const std::string &source);
    ~Program();

    auto run() -> Session*;

  private:
    JSValue m_main;
  };

  //
  // Session
  //

  class Session {
  public:
    Session(JSValue main);
    ~Session();

    static void define(JSContext *ctx);

    void reset(JSContext *ctx);

    void process(
      std::shared_ptr<NS::Context> ctx,
      std::unique_ptr<Object> obj,
      Object::Receiver out
    );

  private:
    JSValue m_main;
    JSValue m_obj;
    JSValue m_context_obj;
    JSValue m_input_func;
    JSValue m_output_func;
    Context* m_context;
    Object::Receiver m_current_receiver;

    static JSClassID s_class_id;

    static auto output(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValue *func_data) -> JSValue;
  };

  //
  // Class
  //

  template<class T>
  class Class {
  public:
    static auto get(JSValue obj) -> T* {
      return static_cast<T*>(JS_GetOpaque(obj, s_class_id));
    }

    static auto make(JSContext *ctx, T* ptr) -> JSValue {
      auto obj = JS_NewObjectClass(ctx, s_class_id);
      JS_SetOpaque(obj, ptr);
      return obj;
    }

  protected:
    static void define_class(JSContext *ctx) {
      if (!s_class_id) s_class_id = JS_NewClassID(&s_class_id);
      JSClassDef cd{ s_class_name, finalize };
      JS_NewClass(JS_GetRuntime(ctx), s_class_id, &cd);
      auto proto = JS_NewObject(ctx);
      JS_SetClassProto(ctx, s_class_id, proto);
    }

    static void define_ctor(JSContext *ctx, JSCFunction *func, int argc, const char *ns = nullptr) {
      auto ctor = JS_NewCFunction2(ctx, func, s_class_name, argc, JS_CFUNC_constructor, 0);
      auto global = JS_GetGlobalObject(ctx);
      auto proto = JS_GetClassProto(ctx, s_class_id);
      if (ns) {
        auto obj = JS_GetPropertyStr(ctx, global, ns);
        if (!JS_IsObject(obj)) {
          obj = JS_NewObject(ctx);
          JS_SetPropertyStr(ctx, global, ns, obj);
        }
        JS_SetPropertyStr(ctx, obj, s_class_name, ctor);
      } else {
        JS_DefinePropertyValueStr(ctx, global, s_class_name, ctor, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
      }
      JS_SetConstructor(ctx, ctor, proto);
      JS_FreeValue(ctx, proto);
      JS_FreeValue(ctx, global);
    }

    static void define_func(JSContext *ctx, const char *name, JSCFunction *func, int argc) {
      auto proto = JS_GetClassProto(ctx, s_class_id);
      JS_DefinePropertyValue(ctx, proto, JS_NewAtom(ctx, name), JS_NewCFunction(ctx, func, name, argc), 0);
      JS_FreeValue(ctx, proto);
    }

    static void define_prop(JSContext *ctx, const char *name, JSCFunction *get, JSCFunction *set) {
      auto proto = JS_GetClassProto(ctx, s_class_id);
      JS_DefinePropertyGetSet(
        ctx, proto, JS_NewAtom(ctx, name),
        get ? JS_NewCFunction(ctx, get, name, 0) : JS_UNDEFINED,
        set ? JS_NewCFunction(ctx, set, name, 1) : JS_UNDEFINED,
        (get ? JS_PROP_HAS_GET : 0) | (set ? JS_PROP_HAS_SET : 0)
      );
      JS_FreeValue(ctx, proto);
    }

  private:
    static JSClassID s_class_id;
    static const char* s_class_name;

    static void finalize(JSRuntime *rt, JSValue this_obj) {
      delete static_cast<T*>(JS_GetOpaque(this_obj, s_class_id));
    }
  };

  //
  // Class utilities
  //

  template<class T> auto get_cpp_obj(JSValue obj) -> T* { return Class<T>::get(obj); }
  template<class T> auto make_js_obj(JSContext *ctx, T* obj) -> JSValue { return Class<T>::make(ctx, obj); }

  //
  // Symbols
  //

  template<class T>
  class Symbols {
  public:
    Symbols() {
      m_worker = Worker::current();
    }

    Symbols(JSContext *ctx) {
      auto rt = JS_GetRuntime(ctx);
      m_worker = static_cast<Worker*>(JS_GetRuntimeOpaque(rt));
    }

    void define(T id, const char *name) {
      if (!s_symbols[(int)id]) {
        s_symbols[(int)id] = m_worker->new_symbol(name);
      }
    }

    auto operator[](T id) -> JSAtom {
      return m_worker->get_symbol(s_symbols[(int)id]);
    }

  private:
    Worker* m_worker;
    static int s_symbols[int(T::__MAX__)];
  };

  //
  // Context
  //

  class Context : public Class<Context>, public Pooled<Context> {
  public:
    static void define(JSContext *ctx);

    std::shared_ptr<NS::Context> context;

  private:
    static auto all(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto get(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto set(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
  };

  //
  // Event
  //

  class Event : public Class<Event>, public Pooled<Event> {
  public:
    static void define(JSContext *ctx);

    enum class Type {
      sessionstart,
      sessionend,
      messagestart,
      messageend,
      mapstart,
      mapkey,
      mapend,
      liststart,
      listend,
      __MAX__,
    };

    Event(Object::Type t) : type(t) {}

    Event(Object::Type t, const std::string &v) : type(t), value(v) {}

    Event(Object *obj) : type(obj->type()) {
      if (type == Object::MapKey) {
        value = obj->as<MapKey>()->key;
      }
    }

    Object::Type type;
    std::string value;

    auto to_object() -> std::unique_ptr<Object>;

  private:
    static auto construct(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto get_type(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto get_value(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
  };

  //
  // Buffer
  //

  class Buffer : public Class<Buffer>, public Pooled<Buffer> {
  public:
    static void define(JSContext *ctx);

    enum class Encoding {
      utf8,
      hex,
      base64,
      __MAX__,
    };

    Buffer();
    Buffer(const Data &&d);
    Buffer(int size);
    Buffer(int size, int value);
    Buffer(const void *buf, int len);
    Buffer(const std::string &str);

    Data data;

  private:
    static auto construct(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto get_size(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto push(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto shift(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto toString(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto toArrayBuffer(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
  };

  //
  // Utilities
  //

  auto get_as_string(JSContext *ctx, JSValue val) -> std::string;
  auto throw_invalid_this_type(JSContext *ctx) -> JSValue;
  auto throw_invalid_argument_type(JSContext *ctx) -> JSValue;
  auto throw_invalid_argument_type(JSContext *ctx, int n) -> JSValue;

  class CStr {
  public:
    CStr(JSContext *ctx, JSValue val) : m_ctx(ctx) {
      ptr = JS_ToCStringLen(ctx, &len, val);
    }

    ~CStr() {
      if (ptr) JS_FreeCString(m_ctx, ptr);
    }

    const char *ptr;
    size_t len;

    operator std::string() {
      return ptr ? std::string(ptr, len) : std::string();
    }

  private:
    JSContext* m_ctx;
  };

} // namespace js

DECLARE_CLASS(js::Event);
DECLARE_CLASS(js::Buffer);

NS_END

#endif // JS_HPP