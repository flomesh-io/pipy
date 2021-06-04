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
#include "data.hpp"
#include "logging.hpp"

#include <sys/stat.h>
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

bool OS::Stats::is_file()             { return S_ISREG(mode); }
bool OS::Stats::is_directory()        { return S_ISDIR(mode); }
bool OS::Stats::is_character_device() { return S_ISCHR(mode); }
bool OS::Stats::is_block_device()     { return S_ISBLK(mode); }
bool OS::Stats::is_fifo()             { return S_ISFIFO(mode); }
bool OS::Stats::is_symbolic_link()    { return S_ISLNK(mode); }
bool OS::Stats::is_socket()           { return S_ISSOCK(mode); }

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<OS>::init() {
  ctor();

  // os.readFile
  method("readFile", [](Context &ctx, Object*, Value &ret) {
    Str *filename;
    if (!ctx.arguments(1, &filename)) return;
    std::ifstream fs(filename->str(), std::ios::in);
    if (!fs.is_open()) {
      Log::error("os.readFile: cannot open file: %s", filename->c_str());
      ret = Value::null;
      return;
    }
    auto *data = pipy::Data::make();
    char buf[1024];
    while (!fs.eof()) {
      fs.read(buf, sizeof(buf));
      data->push(buf, fs.gcount());
    }
    ret.set(data);
  });

  // os.stat
  method("stat", [](Context &ctx, Object*, Value &ret) {
    Str *filename;
    if (!ctx.arguments(1, &filename)) return;
    struct stat st;
    if (stat(filename->c_str(), &st)) {
      ret = Value::null;
    } else {
      auto s = OS::Stats::make();
      s->dev = st.st_dev;
      s->ino = st.st_ino;
      s->mode = st.st_mode;
      s->nlink = st.st_nlink;
      s->uid = st.st_uid;
      s->gid = st.st_gid;
      s->rdev = st.st_rdev;
      s->size = st.st_size;
      s->blksize = st.st_blksize;
      s->blocks = st.st_blocks;
      s->atime = st.st_atime;
      s->mtime = st.st_mtime;
      s->ctime = st.st_ctime;
      ret.set(s);
    }
  });

  // os.env
  accessor("env", [](Object *obj, Value &ret) { ret.set(obj->as<OS>()->env()); });
}

template<> void ClassDef<OS::Stats>::init() {
  accessor("dev",     [](Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->dev); });
  accessor("ino",     [](Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->ino); });
  accessor("mode",    [](Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->mode); });
  accessor("nlink",   [](Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->nlink); });
  accessor("uid",     [](Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->uid); });
  accessor("gid",     [](Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->gid); });
  accessor("rdev",    [](Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->rdev); });
  accessor("size",    [](Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->size); });
  accessor("blksize", [](Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->blksize); });
  accessor("blocks",  [](Object *obj, Value &ret) { ret.set(obj->as<OS::Stats>()->blocks); });
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