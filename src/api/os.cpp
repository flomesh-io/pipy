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

#include "os.hpp"
#include "fs.hpp"
#include "data.hpp"
#include "log.hpp"

#include <fstream>

extern "C" char **environ;

namespace pipy {

OS::OS()
  : m_env(pjs::Object::make())
{
  for (auto e = environ; *e; e++) {
    if (auto p = std::strchr(*e, '=')) {
      std::string name(*e, p - *e);
      m_env->ht_set(name, p + 1);
    }
  }
}

auto OS::platform() -> Platform {
#ifdef _WIN32
  return Platform::windows;
#elif defined(__linux__)
  return Platform::linux;
#elif defined(__APPLE__)
  return Platform::darwin;
#elif defined(__FreeBSD__)
  return Platform::freebsd;
#else
  return Platform::unknown;
#endif
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<OS>::init() {
  ctor();

  // os.readDir
  method("readDir", [](Context &ctx, Object*, Value &ret) {
    Str *pathname;
    if (!ctx.arguments(1, &pathname)) return;
    std::list<std::string> names;
    fs::read_dir(pathname->str(), names);
    auto a = Array::make(names.size());
    auto i = 0;
    for (auto &s : names) {
      a->set(i++, Str::make(std::move(s)));
    }
    ret.set(a);
  });

  // os.readFile
  method("readFile", [](Context &ctx, Object*, Value &ret) {
    thread_local static pipy::Data::Producer s_dp("os.readFile");

    Str *filename;
    if (!ctx.arguments(1, &filename)) return;
    std::ifstream fs(filename->str(), std::ios::in|std::ios::binary);
    if (!fs.is_open()) {
      Log::error("os.readFile: cannot open file: %s", filename->c_str());
      ret = Value::null;
      return;
    }
    auto *data = pipy::Data::make();
    char buf[1024];
    while (fs.good()) {
      fs.read(buf, sizeof(buf));
      s_dp.push(data, buf, fs.gcount());
    }
    ret.set(data);
  });

  // os.writeFile
  method("writeFile", [](Context &ctx, Object*, Value &ret) {
    Str *filename;
    Str *str = nullptr;
    pipy::Data *data = nullptr;
    if (!ctx.try_arguments(2, &filename, &str) &&
        !ctx.arguments(2, &filename, &data))
    {
      return;
    }
    std::ofstream fs(filename->str(), std::ios::out|std::ios::binary|std::ios::trunc);
    if (!fs.is_open()) {
      Log::error("os.writeFile: cannot open file: %s", filename->c_str());
      ret = Value::null;
      return;
    }
    if (str) {
      fs.write(str->c_str(), str->size());
    } else if (data) {
      for (const auto c : data->chunks()) {
        fs.write(std::get<0>(c), std::get<1>(c));
      }
    }
  });

  // os.stat
  method("stat", [](Context &ctx, Object*, Value &ret) {
    Str *filename;
    if (!ctx.arguments(1, &filename)) return;
    auto s = OS::Stats::make();
    if (fs::stat(filename->str(), *s)) {
      ret.set(s);
    } else {
      ret = Value::null;
      s->retain();
      s->release();
    }
  });

  // os.unlink
  method("unlink", [](Context &ctx, Object*, Value &ret) {
    Str *filename;
    if (!ctx.arguments(1, &filename)) return;
    ret.set(fs::unlink(filename->str()));
  });

  // os.env
  accessor("env", [](Object *obj, Value &ret) {
    ret.set(obj->as<OS>()->env());
  });

  // os.platform
  accessor("platform", [](Object *obj, Value &ret) {
    ret.set(EnumDef<OS::Platform>::name(obj->as<OS>()->platform()));
  });
}

template<> void EnumDef<OS::Platform>::init() {
  define(OS::Platform::unknown, "");
  define(OS::Platform::linux, "linux");
  define(OS::Platform::darwin, "darwin");
  define(OS::Platform::windows, "windows");
  define(OS::Platform::freebsd, "freebsd");
}

template<> void ClassDef<OS::Stats>::init() {
  accessor("size",    [](Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->size); });
  accessor("atime",   [](Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->atime); });
  accessor("mtime",   [](Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->mtime); });
  accessor("ctime",   [](Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->ctime); });

  method("isFile",            [](Context&, Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->is_file()); });
  method("isDirectory",       [](Context&, Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->is_directory()); });
  method("isCharacterDevice", [](Context&, Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->is_character_device()); });
  method("isBlockDevice",     [](Context&, Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->is_block_device()); });
  method("isFIFO",            [](Context&, Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->is_fifo()); });
  method("isSymbolicLink",    [](Context&, Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->is_symbolic_link()); });
  method("isSocket",          [](Context&, Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->is_socket()); });
}

} // namespace pjs
