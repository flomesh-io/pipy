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

//
// OS
//

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

auto OS::home() -> std::string {
  return fs::home();
}

auto OS::stat(const std::string &pathname) -> Stats* {
  auto s = OS::Stats::make();
  if (fs::stat(pathname, *s)) {
    return s;
  } else {
    s->retain();
    s->release();
    return nullptr;
  }
}

auto OS::list(const std::string &pathname) -> pjs::Array* {
  std::list<std::string> names;
  fs::read_dir(pathname, names);
  auto a = pjs::Array::make(names.size());
  auto i = 0;
  for (auto &s : names) {
    a->set(i++, pjs::Str::make(std::move(s)));
  }
  return a;
}

auto OS::read(const std::string &pathname) -> Data* {
  thread_local static Data::Producer s_dp("os.read()");
  std::ifstream fs(pathname, std::ios::in|std::ios::binary);
  if (!fs.is_open()) {
    throw std::runtime_error("cannot open file: " + pathname);
  }
  Data data;
  Data::Builder db(data, &s_dp);
  char buf[1024];
  while (fs.good()) {
    fs.read(buf, sizeof(buf));
    db.push(buf, fs.gcount());
  }
  db.flush();
  return Data::make(std::move(data));
}

void OS::write(const std::string &pathname, Data *data) {
  std::ofstream fs(pathname, std::ios::out|std::ios::binary|std::ios::trunc);
  if (!fs.is_open()) {
    throw std::runtime_error("cannot open file: " + pathname);
  }
  if (data) {
    for (const auto c : data->chunks()) {
      fs.write(std::get<0>(c), std::get<1>(c));
    }
  }
}

void OS::write(const std::string &pathname, const std::string &data) {
  std::ofstream fs(pathname, std::ios::out|std::ios::binary|std::ios::trunc);
  if (!fs.is_open()) {
    throw std::runtime_error("cannot open file: " + pathname);
  }
  fs.write(data.c_str(), data.length());
}

void OS::rename(const std::string &old_name, const std::string &new_name) {
  if (!fs::rename(old_name, new_name)) {
    throw std::runtime_error("cannot rename file: " + old_name + " -> " + new_name);
  }
}

bool OS::unlink(const std::string &pathname) {
  return fs::unlink(pathname);
}

void OS::mkdir(const std::string &pathname, const MkdirOptions &options) {
  auto fullpath = fs::abs_path(pathname);
  if (options.recursive) {
    std::function<void(const std::string &)> mk;
    mk = [&](const std::string &pathname) {
      if (fs::is_dir(pathname)) return;
      auto dirname = utils::path_dirname(pathname);
      if (!fs::is_dir(dirname)) {
        if (fs::is_file(dirname)) {
          throw std::runtime_error("cannot create directory: " + dirname);
        }
        mk(dirname);
      }
      if (!fs::make_dir(pathname)) {
        throw std::runtime_error("cannot create directory: " + pathname);
      }
    };
    mk(fullpath);
  } else if (!fs::make_dir(fullpath)) {
    throw std::runtime_error("cannot create directory: " + fullpath);
  }
}

bool OS::rmdir(const std::string &pathname, const RmdirOptions &options) {
  if (options.recursive) {
    return rm(pathname, options);
  } else {
    auto fullpath = fs::abs_path(pathname);
    if (options.force && !fs::exists(fullpath)) return false;
    if (!fs::remove_dir(fullpath)) throw std::runtime_error("cannot delete file: " + fullpath);
    return true;
  }
}

bool OS::rm(const std::string &pathname, const RmdirOptions &options) {
  auto fullpath = fs::abs_path(pathname);
  if (options.force && !fs::exists(fullpath)) return false;
  if (options.recursive) {
    std::function<void(const std::string &)> rm;
    rm = [&](const std::string &pathname) {
      if (fs::is_dir(pathname)) {
        std::list<std::string> names;
        fs::read_dir(pathname, names);
        for (const auto &name : names) rm(utils::path_join(pathname, name));
        if (!fs::remove_dir(pathname)) throw std::runtime_error("cannot delete directory: " + pathname);
      } else {
        if (!fs::unlink(pathname)) throw std::runtime_error("cannot delete file: " + pathname);
      }
    };
    rm(fullpath);
  } else if (!fs::unlink(fullpath)) {
    throw std::runtime_error("cannot delete file: " + fullpath);
  }
  return true;
}

//
// OS::MkdirOptions
//

OS::MkdirOptions::MkdirOptions(pjs::Object *options) {
  Value(options, "recursive")
    .get(recursive)
    .check_nullable();
}

//
// OS::RmdirOptions
//

OS::RmdirOptions::RmdirOptions(pjs::Object *options) : MkdirOptions(options) {
  Value(options, "force")
    .get(force)
    .check_nullable();
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<OS>::init() {
  ctor();

  // os.platform
  accessor("platform", [](Object *obj, Value &ret) {
    ret.set(EnumDef<OS::Platform>::name(OS::platform()));
  });

  // os.env
  accessor("env", [](Object *obj, Value &ret) {
    ret.set(obj->as<OS>()->env());
  });

  // os.home
  method("home", [](Context &ctx, Object*, Value &ret) {
    ret.set(OS::home());
  });

  // os.stat
  method("stat", [](Context &ctx, Object*, Value &ret) {
    Str *filename;
    if (!ctx.arguments(1, &filename)) return;
    ret.set(OS::stat(filename->str()));
  });

  // os.list
  method("list", [](Context &ctx, Object*, Value &ret) {
    Str *pathname;
    if (!ctx.arguments(1, &pathname)) return;
    ret.set(OS::list(pathname->str()));
  });

  // os.read
  method("read", [](Context &ctx, Object*, Value &ret) {
    Str *filename;
    if (!ctx.arguments(1, &filename)) return;
    try {
      ret.set(OS::read(filename->str()));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // os.write
  method("write", [](Context &ctx, Object*, Value &ret) {
    Str *filename;
    Str *str = nullptr;
    pipy::Data *data = nullptr;
    if (!ctx.check(0, filename)) return;
    if (!ctx.get(1, data) && !ctx.get(1, str)) return ctx.error_argument_type(1, "a Data or string");
    try {
      if (str) {
        OS::write(filename->str(), str->str());
      } else {
        OS::write(filename->str(), data);
      }
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // os.rename
  method("rename", [](Context &ctx, Object*, Value &ret) {
    Str *old_name, *new_name;
    if (!ctx.arguments(2, &old_name, &new_name)) return;
    try {
      OS::rename(old_name->str(), new_name->str());
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // os.unlink
  method("unlink", [](Context &ctx, Object*, Value &ret) {
    Str *filename;
    if (!ctx.arguments(1, &filename)) return;
    ret.set(OS::unlink(filename->str()));
  });

  // os.mkdir
  method("mkdir", [](Context &ctx, Object*, Value &ret) {
    Str *pathname;
    Object *options = nullptr;
    if (!ctx.arguments(1, &pathname, &options)) return;
    try {
      OS::mkdir(pathname->str(), options);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // os.rmdir
  method("rmdir", [](Context &ctx, Object*, Value &ret) {
    Str *pathname;
    Object *options = nullptr;
    if (!ctx.arguments(1, &pathname, &options)) return;
    try {
      ret.set(OS::rmdir(pathname->str(), options));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // os.rm
  method("rm", [](Context &ctx, Object*, Value &ret) {
    Str *pathname;
    Object *options = nullptr;
    if (!ctx.arguments(1, &pathname, &options)) return;
    try {
      ret.set(OS::rm(pathname->str(), options));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // os.readDir
  method("readDir", [](Context &ctx, Object*, Value &ret) {
    Str *pathname;
    if (!ctx.arguments(1, &pathname)) return;
    ret.set(OS::list(pathname->str()));
  });

  // os.readFile
  method("readFile", [](Context &ctx, Object*, Value &ret) {
    Str *filename;
    if (!ctx.arguments(1, &filename)) return;
    try {
      ret.set(OS::read(filename->str()));
    } catch (std::runtime_error &err) {
      Log::error("%s", err.what());
      ret = Value::null;
    }
  });

  // os.writeFile
  method("writeFile", [](Context &ctx, Object*, Value &ret) {
    Str *filename;
    Str *str = nullptr;
    pipy::Data *data = nullptr;
    if (!ctx.check(0, filename)) return;
    if (!ctx.get(1, data) && !ctx.get(1, str)) return ctx.error_argument_type(1, "a Data or string");
    try {
      if (str) {
        OS::write(filename->str(), str->str());
      } else {
        OS::write(filename->str(), data);
      }
    } catch (std::runtime_error &err) {
      Log::error("%s", err.what());
    }
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
