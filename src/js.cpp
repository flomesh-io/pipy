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

#include "js.hpp"
#include "crypto.hpp"
#include "logging.hpp"

#include <cstring>
#include <fstream>
#include <sstream>

NS_BEGIN

namespace js {

  //
  // Utilities
  //

  auto get_as_string(JSContext *ctx, JSValue val) -> std::string {
    if (auto buf = Buffer::get(val)) return buf->data.to_string();
    if (JS_IsString(val)) return CStr(ctx, val);
    return std::string();
  }

  auto throw_invalid_this_type(JSContext *ctx) -> JSValue {
    return JS_ThrowTypeError(ctx, "invalid type of this object");
  }

  auto throw_invalid_argument_type(JSContext *ctx) -> JSValue {
    return JS_ThrowTypeError(ctx, "invalid type of argument");
  }

  auto throw_invalid_argument_type(JSContext *ctx, int n) -> JSValue {
    return JS_ThrowTypeError(ctx, "invalid type of argument #%d", n);
  }

  //
  // Worker
  //

  thread_local Worker* Worker::s_worker = nullptr;
  std::vector<std::string> Worker::s_symbols(1);

  extern "C" char **environ;

  auto Worker::current() -> Worker* {
    if (!s_worker) s_worker = new Worker();
    return s_worker;
  }

  Worker::Worker() {
    m_rt = JS_NewRuntime();
    m_ctx = JS_NewContext(m_rt);

    JS_SetRuntimeOpaque(m_rt, this);
    JS_SetModuleLoaderFunc(m_rt, nullptr, module_loader, this);

    m_symbols.resize(s_symbols.size());
    for (size_t i = 0; i < s_symbols.size(); i++) {
      const auto &str = s_symbols[i];
      m_symbols[i] = JS_NewAtomLen(m_ctx, str.c_str(), str.length());
    }

    auto global = JS_GetGlobalObject(m_ctx);

    auto console = JS_NewObject(m_ctx);
    JS_SetPropertyStr(m_ctx, console, "log", JS_NewCFunction(m_ctx, console_log, "log", 1));
    JS_SetPropertyStr(m_ctx, global, "console", console);

    auto process = JS_NewObject(m_ctx);
    auto env = JS_NewObject(m_ctx);
    for (auto e = environ; *e; e++) {
      if (auto p = std::strchr(*e, '=')) {
        std::string name(*e, p - *e);
        JS_SetPropertyStr(m_ctx, env, name.c_str(), JS_NewString(m_ctx, p + 1));
      }
    }
    JS_SetPropertyStr(m_ctx, process, "env", env);
    JS_SetPropertyStr(m_ctx, global, "process", process);
    JS_FreeValue(m_ctx, global);

    Session::define(m_ctx);
    Context::define(m_ctx);
    Event::define(m_ctx);
    Buffer::define(m_ctx);

    crypto::Sign::define(m_ctx);
    crypto::Verify::define(m_ctx);
    crypto::Cipher::define(m_ctx);
    crypto::Decipher::define(m_ctx);
  }

  Worker::~Worker() {
    JS_FreeContext(m_ctx);
    JS_FreeRuntime(m_rt);
  }

  auto Worker::module_loader(JSContext *ctx, const char *module_name, void *opaque) -> JSModuleDef* {
    Log::info("Loading module %s", module_name);

    auto path = static_cast<Worker*>(opaque)->m_root_path + '/' + module_name;
    std::ifstream fs(path, std::ios::in);
    if (!fs.is_open()) {
      auto msg = std::string("cannot open file ") + module_name;
      throw std::runtime_error(msg);
    }

    std::stringstream ss;
    fs >> ss.rdbuf();

    auto code = ss.str();
    auto compiled = JS_Eval(ctx, code.c_str(), code.length(), module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

    if (JS_IsException(compiled)) {
      auto val = JS_GetException(ctx);
      auto str = JS_ToCString(ctx, val);
      auto stk = JS_ToCString(ctx, JS_GetPropertyStr(ctx, val, "stack"));
      auto msg = std::string(str) + stk;
      JS_FreeCString(ctx, str);
      JS_FreeCString(ctx, stk);
      JS_FreeValue(ctx, compiled);
      throw std::runtime_error(msg);

    } else if (JS_VALUE_GET_TAG(compiled) == JS_TAG_MODULE) {
      JSModuleDef *m = (JSModuleDef*)JS_VALUE_GET_PTR(compiled);
      JS_FreeValue(ctx, compiled);
      return m;

    } else {
      JS_FreeValue(ctx, compiled);
      auto msg = std::string("cannot eval ") + module_name;
      throw std::runtime_error(msg);
    }

    return nullptr;
  }

  auto Worker::console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) -> JSValue {
    std::string line;
    for (int i = 0; i < argc; i++) {
      if (i > 0) line += ' ';
      auto str = JS_ToCString(ctx, argv[i]);
      if (!str) return JS_EXCEPTION;
      line += str;
      JS_FreeCString(ctx, str);
    }
    Log::info("[js] %s", line.c_str());
    return JS_UNDEFINED;
  }

  //
  // Program
  //

  Program::Program(const std::string &source) {
    auto worker = Worker::current();
    auto rt = worker->runtime();
    auto ctx = worker->context();

    char main[1000];
    std::sprintf(main, "var main; import('%s').then(m => main = m.default);", source.c_str());
    auto result = JS_Eval(ctx, main, std::strlen(main), "[main]", JS_EVAL_TYPE_GLOBAL);
    while (JS_IsJobPending(rt)) {
      JSContext *ctx;
      JS_ExecutePendingJob(rt, &ctx);
    }

    if (JS_IsException(result)) {
      auto val = JS_GetException(ctx);
      auto str = JS_ToCString(ctx, val);
      auto msg = std::string("exception in main: ") + str;
      throw std::runtime_error(msg);
    }

    auto g = JS_GetGlobalObject(ctx);
    m_main = JS_GetPropertyStr(ctx, g, "main");
    JS_FreeValue(ctx, g);

    if (!JS_IsFunction(ctx, m_main)) {
      auto msg = source + " does not export a default function";
      throw std::runtime_error(msg);
    }
  }

  auto Program::run() -> Session* {
    return new Session(m_main);
  }

  //
  // Session
  //

  JSClassID Session::s_class_id = 0;

  Session::Session(JSValue main) : m_main(main) {
    auto worker = Worker::current();
    auto rt = worker->runtime();
    auto ctx = worker->context();

    m_obj = JS_NewObjectClass(ctx, s_class_id);
    m_context = new Context();
    m_context_obj = Context::make(ctx, m_context);
    m_input_func = JS_UNDEFINED;
    m_output_func = JS_NewCFunctionData(ctx, output, 1, 0, 1, &m_obj);
    JS_SetOpaque(m_obj, this);
  }

  Session::~Session() {
    auto worker = Worker::current();
    auto ctx = worker->context();
    reset(ctx);
    JS_FreeValue(ctx, m_obj);
    JS_FreeValue(ctx, m_context_obj);
    JS_FreeValue(ctx, m_output_func);
  }

  void Session::define(JSContext *ctx) {
    if (!s_class_id) s_class_id = JS_NewClassID(&s_class_id);
  }

  void Session::reset(JSContext *ctx) {
    JS_FreeValue(ctx, m_input_func);
    m_input_func = JS_UNDEFINED;
  }

  void Session::process(
    std::shared_ptr<NS::Context> context,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) {
    auto worker = Worker::current();
    auto rt = worker->runtime();
    auto ctx = worker->context();

    bool is_session_end = false;

    if (obj->is<SessionStart>()) {
      reset(ctx);

      const auto &c = *context;
      JS_SetPropertyStr(ctx, m_context_obj, "remoteAddress", JS_NewStringLen(ctx, c.remote_addr.c_str(), c.remote_addr.length()));
      JS_SetPropertyStr(ctx, m_context_obj, "localAddress", JS_NewStringLen(ctx, c.local_addr.c_str(), c.local_addr.length()));
      JS_SetPropertyStr(ctx, m_context_obj, "remotePort", JS_NewInt32(ctx, c.remote_port));
      JS_SetPropertyStr(ctx, m_context_obj, "localPort", JS_NewInt32(ctx, c.local_port));

      JSValue argv[2];
      argv[0] = m_output_func;
      argv[1] = m_context_obj;
      m_input_func = JS_Call(ctx, m_main, JS_UNDEFINED, 2, argv);

      if (JS_IsException(m_input_func)) {
        auto value = JS_GetException(ctx);
        auto stack = JS_GetPropertyStr(ctx, value, "stack");
        auto s = JS_ToCString(ctx, value);
        Log::error("[js] %s", s);
        if (!JS_IsUndefined(stack)) {
          auto s = JS_ToCString(ctx, stack);
          Log::error("%s", s);
          JS_FreeCString(ctx, s);
        }
        JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, stack);
        JS_FreeValue(ctx, value);
        JS_FreeValue(ctx, m_input_func);
        m_input_func = JS_UNDEFINED;
      }

    } else if (obj->is<SessionEnd>()) {
      is_session_end = true;
    }

    if (JS_IsUndefined(m_input_func)) return;

    m_current_receiver = out;
    m_context->context = context;

    JSValue input;
    switch (obj->type()) {
      case Object::NullValue:
        input = JS_NULL;
        break;
      case Object::BoolValue:
        input = obj->as<BoolValue>()->value ? JS_TRUE : JS_FALSE;
        break;
      case Object::IntValue:
        input = JS_NewInt32(ctx, obj->as<IntValue>()->value);
        break;
      case Object::LongValue:
        input = JS_NewInt32(ctx, obj->as<LongValue>()->value);
        break;
      case Object::DoubleValue:
        input = JS_NewFloat64(ctx, obj->as<DoubleValue>()->value);
        break;
      case Object::StringValue:
        input = JS_NewString(ctx, obj->as<StringValue>()->value.c_str());
        break;
      case Object::MapStart:
      case Object::MapKey:
      case Object::MapEnd:
      case Object::ListStart:
      case Object::ListEnd:
      case Object::MessageStart:
      case Object::MessageEnd:
      case Object::SessionStart:
      case Object::SessionEnd: {
        input = Event::make(ctx, new Event(obj.get()));
        break;
      case Object::Data:
        input = Buffer::make(ctx, new Buffer(std::move(*obj->as<Data>())));
        break;
      default:
        return;
      }
    }

    auto result = JS_Call(ctx, m_input_func, JS_UNDEFINED, 1, &input);

    if (JS_IsException(result)) {
      auto value = JS_GetException(ctx);
      auto stack = JS_GetPropertyStr(ctx, value, "stack");
      auto s = JS_ToCString(ctx, value);
      Log::error("[js] %s", s);
      if (!JS_IsUndefined(stack)) {
        auto s = JS_ToCString(ctx, stack);
        Log::error("%s", s);
        JS_FreeCString(ctx, s);
      }
      JS_FreeCString(ctx, s);
      JS_FreeValue(ctx, stack);
      JS_FreeValue(ctx, value);
    } else {
      while (JS_IsJobPending(rt)) {
        JSContext *ctx;
        JS_ExecutePendingJob(rt, &ctx);
      }
    }

    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, input);

    if (is_session_end) {
      reset(ctx);
    }
  }

  auto Session::output(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValue *func_data) -> JSValue {
    auto p = static_cast<Session*>(JS_GetOpaque(func_data[0], s_class_id));
    if (auto out = p->m_current_receiver) {
      for (int i = 0; i == 0 || i < argc; i++) {
        auto arg = argv[i];
        switch (JS_VALUE_GET_TAG(arg)) {
          case JS_TAG_NULL:
            out(make_object<NullValue>());
            break;
          case JS_TAG_BOOL:
            out(make_object<BoolValue>(JS_VALUE_GET_BOOL(arg)));
            break;
          case JS_TAG_INT:
            out(make_object<IntValue>(JS_VALUE_GET_INT(arg)));
            break;
          case JS_TAG_FLOAT64:
            out(make_object<DoubleValue>(JS_VALUE_GET_FLOAT64(arg)));
            break;
          case JS_TAG_STRING:
          case JS_TAG_SYMBOL:
            out(make_object<StringValue>(CStr(ctx, arg)));
            break;
          case JS_TAG_OBJECT:
            if (auto e = Event::get(arg)) {
              out(e->to_object());
            } else if (auto b = Buffer::get(arg)) {
              out(make_object<Data>(std::move(b->data)));
            } else {
              return throw_invalid_argument_type(ctx, i+1);
            }
            break;
          default:
            return throw_invalid_argument_type(ctx, i+1);
        }
      }
    }
    return JS_UNDEFINED;
  }

  //
  // Context
  //

  DEFINE_CLASS(Context);

  void Context::define(JSContext *ctx) {
    define_class(ctx);
    DEFINE_FUNC(ctx, all, 1);
    DEFINE_FUNC(ctx, get, 1);
    DEFINE_FUNC(ctx, set, 2);
  }

  auto Context::all(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto c = get_cpp_obj<Context>(this_obj);
    if (!c) return JS_UNDEFINED;
    auto obj = JS_NewObject(ctx);
    if (JS_IsUndefined(argv[0])) {
      for (const auto &p : c->context->variables) {
        const auto &k = p.first;
        const auto &v = p.second;
        JS_SetPropertyStr(ctx, obj, k.c_str(), JS_NewStringLen(ctx, v.c_str(), v.length()));
      }
    } else {
      CStr prefix(ctx, argv[0]);
      for (const auto &p : c->context->variables) {
        const auto &k = p.first;
        const auto &v = p.second;
        if (!std::strncmp(k.c_str(), prefix.ptr, prefix.len)) {
          JS_SetPropertyStr(ctx, obj, k.c_str(), JS_NewStringLen(ctx, v.c_str(), v.length()));
        }
      }
    }
    return obj;
  }

  auto Context::get(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto c = get_cpp_obj<Context>(this_obj);
    if (!c) return JS_UNDEFINED;
    std::string val;
    auto found = c->context->find(CStr(ctx, argv[0]), val);
    if (found) return JS_NewStringLen(ctx, val.c_str(), val.length());
    return JS_UNDEFINED;
  }

  auto Context::set(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto c = get_cpp_obj<Context>(this_obj);
    if (!c) return JS_UNDEFINED;
    CStr key(ctx, argv[0]);
    CStr val(ctx, argv[1]);
    c->context->variables[key] = val;
    return JS_UNDEFINED;
  }

  //
  // Event
  //

  DEFINE_CLASS(Event);
  DEFINE_SYMBOLS(Event::Type);

  void Event::define(JSContext *ctx) {
    Symbols<Type> s(ctx);
    s.DEFINE_SYMBOL(Type, sessionstart);
    s.DEFINE_SYMBOL(Type, sessionend);
    s.DEFINE_SYMBOL(Type, messagestart);
    s.DEFINE_SYMBOL(Type, messageend);
    s.DEFINE_SYMBOL(Type, mapstart);
    s.DEFINE_SYMBOL(Type, mapkey);
    s.DEFINE_SYMBOL(Type, mapend);
    s.DEFINE_SYMBOL(Type, liststart);
    s.DEFINE_SYMBOL(Type, listend);
    define_class(ctx);
    define_ctor(ctx, construct, 2);
    DEFINE_GET(ctx, type);
    DEFINE_GET(ctx, value);
  }

  auto Event::to_object() -> std::unique_ptr<Object> {
    switch (type) {
      case Object::SessionStart: return make_object<SessionStart>();
      case Object::SessionEnd: return make_object<SessionEnd>();
      case Object::MessageStart: return make_object<MessageStart>();
      case Object::MessageEnd: return make_object<MessageEnd>();
      case Object::MapStart: return make_object<MapStart>();
      case Object::MapKey: return make_object<MapKey>(value);
      case Object::MapEnd: return make_object<MapEnd>();
      case Object::ListStart: return make_object<ListStart>();
      case Object::ListEnd: return make_object<ListEnd>();
      default: return nullptr;
    }
  }

  auto Event::construct(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    Symbols<Type> S(ctx);
    auto type = JS_ValueToAtom(ctx, argv[0]);
    Event *e = nullptr;
    if (type == S[Type::mapkey]) e = new Event(Object::MapKey, CStr(ctx, argv[1]));
    else if (type == S[Type::sessionstart]) e = new Event(Object::SessionStart);
    else if (type == S[Type::sessionend]) e = new Event(Object::SessionEnd);
    else if (type == S[Type::messagestart]) e = new Event(Object::MessageStart);
    else if (type == S[Type::messageend]) e = new Event(Object::MessageEnd);
    else if (type == S[Type::mapstart]) e = new Event(Object::MapStart);
    else if (type == S[Type::mapend]) e = new Event(Object::MapEnd);
    else if (type == S[Type::liststart]) e = new Event(Object::ListStart);
    else if (type == S[Type::listend]) e = new Event(Object::ListEnd);
    if (e) {
      return make(ctx, e);
    } else {
      return JS_ThrowTypeError(ctx, "invalid event type");
    }
  }

  auto Event::get_type(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    Symbols<Type> S(ctx);
    auto p = get_cpp_obj<Event>(this_obj);
    if (!p) return throw_invalid_this_type(ctx);
    switch (p->type) {
      case Object::SessionStart: return JS_AtomToString(ctx, S[Type::sessionstart]);
      case Object::SessionEnd: return JS_AtomToString(ctx, S[Type::sessionend]);
      case Object::MessageStart: return JS_AtomToString(ctx, S[Type::messagestart]);
      case Object::MessageEnd: return JS_AtomToString(ctx, S[Type::messageend]);
      case Object::MapStart: return JS_AtomToString(ctx, S[Type::mapstart]);
      case Object::MapKey: return JS_AtomToString(ctx, S[Type::mapkey]);
      case Object::MapEnd: return JS_AtomToString(ctx, S[Type::mapend]);
      case Object::ListStart: return JS_AtomToString(ctx, S[Type::liststart]);
      case Object::ListEnd: return JS_AtomToString(ctx, S[Type::listend]);
      default: return JS_UNDEFINED;
    }
  }

  auto Event::get_value(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto p = get_cpp_obj<Event>(this_obj);
    if (!p) return throw_invalid_this_type(ctx);
    if (p->type == Object::MapKey) return JS_NewStringLen(ctx, p->value.c_str(), p->value.length());
    return JS_UNDEFINED;
  }

  //
  // Buffer
  //

  DEFINE_CLASS(Buffer);
  DEFINE_SYMBOLS(Buffer::Encoding);

  Buffer::Buffer() {
  }

  Buffer::Buffer(const Data &&d) : data(d) {
  }

  Buffer::Buffer(int size) : data(size) {
  }

  Buffer::Buffer(int size, int value) : data(size, value) {
  }

  Buffer::Buffer(const void *buf, int len) : data(buf, len) {
  }

  Buffer::Buffer(const std::string &str) : data(str) {
  }

  void Buffer::define(JSContext *ctx) {
    Symbols<Encoding> s(ctx);
    s.DEFINE_SYMBOL(Encoding, utf8);
    s.DEFINE_SYMBOL(Encoding, hex);
    s.DEFINE_SYMBOL(Encoding, base64);
    define_class(ctx);
    define_ctor(ctx, construct, 2);
    DEFINE_GET(ctx, size);
    DEFINE_FUNC(ctx, push, 1);
    DEFINE_FUNC(ctx, shift, 1);
    DEFINE_FUNC(ctx, toString, 1);
    DEFINE_FUNC(ctx, toArrayBuffer, 0);
  }

  auto Buffer::construct(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto arg = argv[0];
    switch (JS_VALUE_GET_TAG(arg)) {
      case JS_TAG_UNDEFINED:
        return make(ctx, new Buffer());
      case JS_TAG_INT:
        return make(ctx, new Buffer(JS_VALUE_GET_INT(arg), 0));
      case JS_TAG_FLOAT64:
        return make(ctx, new Buffer((int)JS_VALUE_GET_FLOAT64(arg), 0));
      case JS_TAG_STRING:
      case JS_TAG_SYMBOL: {
        Buffer *buf = nullptr;
        if (JS_IsUndefined(argv[1])) {
          buf = new Buffer(CStr(ctx, arg));
        } else {
          if (!JS_IsString(argv[1])) return throw_invalid_argument_type(ctx, 2);
          Symbols<Encoding> s(ctx);
          auto enc = JS_ValueToAtom(ctx, argv[1]);
          if (enc == s[Encoding::utf8]) {
            buf = new Buffer(CStr(ctx, arg));
          } else if (enc == s[Encoding::hex]) {
            CStr str(ctx, arg);
            if (str.len % 2) return JS_ThrowTypeError(ctx, "incomplete hex string");
            uint8_t raw[str.len>>1];
            for (size_t i = 0; i < str.len; i += 2) {
              auto h = str.ptr[i+0];
              auto l = str.ptr[i+1];
              if ('0' <= h && h <= '9') h -= '0';
              else if ('a' <= h && h <= 'f') h -= 'a' - 10;
              else if ('A' <= h && h <= 'F') h -= 'A' - 10;
              else return JS_ThrowTypeError(ctx, "invalid hex string");
              if ('0' <= l && l <= '9') l -= '0';
              else if ('a' <= l && l <= 'f') l -= 'a' - 10;
              else if ('A' <= l && l <= 'F') l -= 'A' - 10;
              else return JS_ThrowTypeError(ctx, "invalid hex string");
              raw[i>>1] = (h << 4) | l;
            }
            buf = new Buffer(raw, str.len>>1);
          } else if (enc == s[Encoding::base64]) {
            CStr str(ctx, arg);
            if (str.len % 4 > 0) return JS_ThrowTypeError(ctx, "invalid base64 encoding");
            uint8_t raw[str.len / 4 * 3];
            uint32_t w = 0, n = 0, c = 0;
            for (int i = 0; i < str.len; i++) {
              int ch = str.ptr[i];
              if (ch == '=') {
                if (n == 3 && i + 1 == str.len) {
                  raw[c++] = (w >> 10) & 255;
                  raw[c++] = (w >> 2) & 255;
                  break;
                } else if (n == 2 && i + 2 == str.len && str.ptr[i+1] == '=') {
                  raw[c++] = (w >> 4) & 255;
                  break;
                } else {
                  return JS_ThrowTypeError(ctx, "invalid base64 encoding");
                }
              }
              else if (ch == '+') ch = 62;
              else if (ch == '/') ch = 63;
              else if ('0' <= ch && ch <= '9') ch = ch - '0' + 52;
              else if ('a' <= ch && ch <= 'z') ch = ch - 'a' + 26;
              else if ('A' <= ch && ch <= 'Z') ch = ch - 'A';
              else return JS_ThrowTypeError(ctx, "invalid base64 encoding");
              w = (w << 6) | ch;
              if (++n == 4) {
                raw[c++] = (w >> 16) & 255;
                raw[c++] = (w >> 8) & 255;
                raw[c++] = (w >> 0) & 255;
                w = n = 0;
              }
            }
            buf = new Buffer(raw, c);
          } else {
            return JS_ThrowTypeError(ctx, "undefined encoding");
          }
        }
        return make(ctx, buf);
      }
      case JS_TAG_OBJECT: {
        size_t size, offset, length;
        auto buf = JS_GetArrayBuffer(ctx, &size, arg);
        if (buf) return make(ctx, new Buffer(Data(buf, size)));
        auto obj = JS_GetTypedArrayBuffer(ctx, arg, &offset, &length, nullptr);
        if (JS_IsObject(obj)) {
          buf = JS_GetArrayBuffer(ctx, &size, obj);
          Data data((uint8_t*)buf + offset, length);
          JS_FreeValue(ctx, obj);
          return make(ctx, new Buffer(std::move(data)));
        }
        break;
      }
    }
    return throw_invalid_argument_type(ctx);
  }

  auto Buffer::get_size(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto p = get_cpp_obj<Buffer>(this_obj);
    if (!p) return throw_invalid_this_type(ctx);
    return JS_NewInt32(ctx, p->data.size());
  }

  auto Buffer::push(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto p = get_cpp_obj<Buffer>(this_obj);
    if (!p) return throw_invalid_this_type(ctx);
    auto arg = argv[0];
    switch (JS_VALUE_GET_TAG(arg)) {
      case JS_TAG_INT:
      case JS_TAG_FLOAT64: {
        int32_t n;
        if (JS_ToInt32(ctx, &n, arg) < 0) return throw_invalid_argument_type(ctx);
        p->data.push((char)n);
        break;
      }
      case JS_TAG_STRING:
      case JS_TAG_SYMBOL: {
        CStr str(ctx, arg);
        p->data.push(std::string(str));
        break;
      }
      case JS_TAG_OBJECT: {
        if (auto b = Buffer::get(arg)) {
          p->data.push(Data(b->data));
        } else {
          size_t size, offset, length;
          auto buf = JS_GetArrayBuffer(ctx, &size, arg);
          if (buf) {
            p->data.push(buf, size);
          } else {
            auto obj = JS_GetTypedArrayBuffer(ctx, arg, &offset, &length, nullptr);
            if (JS_IsObject(obj)) {
              buf = JS_GetArrayBuffer(ctx, &size, obj);
              p->data.push((uint8_t*)buf + offset, length);
              JS_FreeValue(ctx, obj);
            } else {
              return throw_invalid_argument_type(ctx);
            }
          }
        }
        break;
      }
      default: return throw_invalid_argument_type(ctx);
    }
    return JS_UNDEFINED;
  }

  auto Buffer::shift(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto p = get_cpp_obj<Buffer>(this_obj);
    if (!p) return throw_invalid_this_type(ctx);
    auto arg = argv[0];
    if (JS_IsFunction(ctx, arg)) {
      JSValue exception = JS_UNDEFINED;
      auto cb = arg;
      auto ret = p->data.shift([&](int byte) -> bool {
        auto arg = JS_NewInt32(ctx, byte);
        auto ret = JS_Call(ctx, cb, JS_UNDEFINED, 1, &arg);
        if (JS_IsException(ret)) {
          exception = ret;
          return true;
        }
        return JS_ToBool(ctx, ret);
      });
      return JS_IsUndefined(exception) ? make(ctx, new Buffer(std::move(ret))) : exception;
    } else if (JS_IsNumber(arg)) {
      int32_t n;
      if (JS_ToInt32(ctx, &n, arg) < 0) return throw_invalid_argument_type(ctx);
      if (n < 0 || n > p->data.size()) return JS_ThrowRangeError(ctx, "out of range");
      return make(ctx, new Buffer(p->data.shift(n)));
    }
    return throw_invalid_argument_type(ctx);
  }

  auto Buffer::toString(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto p = get_cpp_obj<Buffer>(this_obj);
    if (!p) return throw_invalid_this_type(ctx);
    if (JS_IsUndefined(argv[0])) {
      auto str = p->data.to_string();
      return JS_NewStringLen(ctx, str.c_str(), str.length());
    } else {
      if (!JS_IsString(argv[0])) return throw_invalid_argument_type(ctx);
      Symbols<Encoding> s(ctx);
      auto enc = JS_ValueToAtom(ctx, argv[0]);
      if (enc == s[Encoding::utf8]) {
        auto str = p->data.to_string();
        return JS_NewStringLen(ctx, str.c_str(), str.length());
      } else if (enc == s[Encoding::hex]) {
        static char tab[] = { "0123456789abcdef" };
        char str[p->data.size() << 1];
        int n = 0;
        p->data.to_chunks([&](const uint8_t *buf, int len) {
          for (int i = 0; i < len; i++) {
            auto b = buf[i];
            str[n++] = tab[b>>4];
            str[n++] = tab[b&15];
          }
        });
        return JS_NewStringLen(ctx, str, n);
      } else if (enc == s[Encoding::base64]) {
        static char tab[] = {
          "ABCDEFGHIJKLMNOP"
          "QRSTUVWXYZabcdef"
          "ghijklmnopqrstuv"
          "wxyz0123456789+/"
        };
        char str[p->data.size() / 3 * 4 + 4];
        uint32_t w = 0, n = 0, c = 0;
        p->data.to_chunks([&](const uint8_t *buf, int len) {
          for (int i = 0; i < len; i++) {
            w = (w << 8) | buf[i];
            if (++n == 3) {
              str[c++] = tab[(w >> 18) & 63];
              str[c++] = tab[(w >> 12) & 63];
              str[c++] = tab[(w >> 6) & 63];
              str[c++] = tab[(w >> 0) & 63];
              w = n = 0;
            }
          }
        });
        switch (n) {
          case 1:
            w <<= 16;
            str[c++] = tab[(w >> 18) & 63];
            str[c++] = tab[(w >> 12) & 63];
            str[c++] = '=';
            str[c++] = '=';
            break;
          case 2:
            w <<= 8;
            str[c++] = tab[(w >> 18) & 63];
            str[c++] = tab[(w >> 12) & 63];
            str[c++] = tab[(w >> 6) & 63];
            str[c++] = '=';
            break;
        }
        return JS_NewStringLen(ctx, str, c);
      } else {
        return JS_ThrowTypeError(ctx, "undefined encoding");
      }
    }
  }

  auto Buffer::toArrayBuffer(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto p = get_cpp_obj<Buffer>(this_obj);
    if (!p) return throw_invalid_this_type(ctx);
    auto obj = JS_NewArrayBufferCopy(ctx, nullptr, p->data.size());
    if (!JS_IsException(obj)) {
      size_t size;
      auto buf = JS_GetArrayBuffer(ctx, &size, obj);
      p->data.to_bytes(buf);
    }
    return obj;
  }

} // namespace js

NS_END
