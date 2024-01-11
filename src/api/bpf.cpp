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

#include "api/bpf.hpp"
#include "elf.hpp"
#include "log.hpp"

#include <cstring>

#ifdef PIPY_USE_BPF

#include <unistd.h>
#include <elf.h>
#include <sys/syscall.h>
#include <linux/bpf.h>

#define attr_size(FIELD) \
  (offsetof(bpf_attr, FIELD) + sizeof(bpf_attr::FIELD))

#endif // PIPY_USE_BPF

namespace pipy {
namespace bpf {

thread_local static Data::Producer s_dp("BPF");

#ifdef PIPY_USE_BPF

static inline int syscall_bpf(enum bpf_cmd cmd, union bpf_attr *attr, unsigned int size) {
	return syscall(__NR_bpf, cmd, attr, size);
}

static inline int syscall_bpf(enum bpf_cmd cmd, union bpf_attr *attr, unsigned int size,
  const std::function<void(union bpf_attr &attr)> &in
) {
  std::memset(attr, 0, size);
  in(*attr);
  return syscall_bpf(cmd, attr, size);
}

static void syscall_error(const char *name) {
  char msg[200];
  std::snprintf(msg, sizeof(msg), "syscall %s failed with errno = %d: ", name, errno);
  throw std::runtime_error(std::string(msg) + std::strerror(errno));
}

//
// ObjectFile
//

ObjectFile::ObjectFile(Data *data) {
  auto bytes = data->to_bytes();
  ELF obj(std::move(bytes));

  for (size_t i = 0; i < obj.symbols.size(); i++) {
    const auto &sym = obj.symbols[i];
    if (sym.type != STT_FUNC) continue;
    if (sym.shndx >= obj.sections.size()) continue;
    const auto &sec = obj.sections[sym.shndx];
    if (sec.type != SHT_PROGBITS) continue;
    if (!(sec.flags & SHF_EXECINSTR)) continue;
    auto offset = sym.value;
    auto length = sym.size;
    if (offset + length > sec.size) continue;
    programs.push_back(
      Program::make(sec.name, sec.data + offset, length)
    );
  }
}

//
// Program
//

Program::Program(const std::string &name, const void *insts, size_t size)
  : m_name(pjs::Str::make(name))
  , m_insts(size)
  , m_inst_count(size / sizeof(struct bpf_insn))
{
  std::memcpy(m_insts.data(), insts, size);
}

auto Program::list() -> pjs::Array* {
  return nullptr;
}

auto Program::maps() -> pjs::Array* {
  return nullptr;
}

void Program::load() {
  std::vector<char> log_buf(100*1024);
  union bpf_attr attr;
  int fd = syscall_bpf(
    BPF_PROG_LOAD, &attr, attr_size(fd_array),
    [&](union bpf_attr &attr) {
      std::strncpy(attr.prog_name, m_name->c_str(), sizeof(attr.prog_name));
      attr.prog_type = BPF_PROG_TYPE_XDP;
      attr.insn_cnt = m_inst_count;
      attr.insns = (uintptr_t)m_insts.data();
      attr.license = (uintptr_t)"GPL";
      attr.log_level = 1;
      attr.log_size = log_buf.size();
      attr.log_buf = (uintptr_t)log_buf.data();
      attr.prog_ifindex = 0;
    }
  );
  if (log_buf[0] && Log::is_enabled(Log::BPF)) {
    Log::debug(Log::BPF, "[bpf] In-kernel verifier log:");
    Log::write(log_buf.data());
  }
  if (fd < 0) {
    syscall_error("BPF_PROG_LOAD");
  }
  m_fd = fd;
}

//
// Map
//

Map::Map(int fd, CStruct *key_type, CStruct *value_type)
  : m_fd(fd)
  , m_key_type(key_type)
  , m_value_type(value_type)
{
  union bpf_attr attr;
  struct bpf_map_info info = {};
  syscall_bpf(
    BPF_OBJ_GET_INFO_BY_FD, &attr, attr_size(info),
    [&](union bpf_attr &attr) {
      attr.info.bpf_fd = fd;
      attr.info.info_len = sizeof(info);
      attr.info.info = (uint64_t)&info;
    }
  );
  m_key_size = info.key_size;
  m_value_size = info.value_size;
}

auto Map::list() -> pjs::Array* {
  auto a = pjs::Array::make();
  union bpf_attr attr;
  unsigned int id = 0;
  try {
    for (;;) {
      if (!syscall_bpf(
        BPF_MAP_GET_NEXT_ID, &attr, attr_size(open_flags),
        [&](union bpf_attr &attr) { attr.start_id = id; }
      )) {
        id = attr.next_id;
      } else {
        if (errno != ENOENT) syscall_error("BPF_MAP_GET_NEXT_ID");
        break;
      }

      int fd = syscall_bpf(
        BPF_MAP_GET_FD_BY_ID, &attr, attr_size(open_flags),
        [&](union bpf_attr &attr) { attr.map_id = id; }
      );

      if (fd < 0) {
        a->retain();
        a->release();
        syscall_error("BPF_MAP_GET_FD_BY_ID");
      }

      struct bpf_map_info info = {};
      if (syscall_bpf(
        BPF_OBJ_GET_INFO_BY_FD, &attr, attr_size(info),
        [&](union bpf_attr &attr) {
          attr.info.bpf_fd = fd;
          attr.info.info_len = sizeof(info);
          attr.info.info = (uint64_t)&info;
        }
      )) {
        syscall_error("BPF_OBJ_GET_INFO_BY_FD");
      }
      ::close(fd);

      auto i = Info::make();
      i->name = pjs::Str::make(info.name);
      i->id = info.id;
      i->flags = info.map_flags;
      i->maxEntries = info.max_entries;
      i->keySize = info.key_size;
      i->valueSize = info.value_size;

      a->push(i);
    }
  } catch (std::runtime_error &err) {
    a->retain();
    a->release();
    throw;
  }
  return a;
}

auto Map::open(int id, CStruct *key_type, CStruct *value_type) -> Map* {
  union bpf_attr attr;
  int fd = syscall_bpf(
    BPF_MAP_GET_FD_BY_ID, &attr, attr_size(open_flags),
    [&](union bpf_attr &attr) { attr.map_id = id; }
  );
  if (fd < 0) {
    syscall_error("BPF_MAP_GET_FD_BY_ID");
  }
  return Map::make(fd, key_type, value_type);
}

auto Map::keys() -> pjs::Array* {
  if (!m_fd) return nullptr;

  uint8_t k[m_key_size];

  auto a = pjs::Array::make();
  uint8_t *p = nullptr;

  union bpf_attr attr;
  while (!syscall_bpf(
    BPF_MAP_GET_NEXT_KEY, &attr, attr_size(next_key),
    [&](union bpf_attr &attr) {
      attr.map_fd = m_fd;
      attr.key = (uintptr_t)p;
      attr.next_key = (uintptr_t)k;
    }
  )) {
    Data data(k, m_key_size, &s_dp);
    if (m_key_type) {
      a->push(m_key_type->decode(data));
    } else {
      a->push(Data::make(std::move(data)));
    }
    p = k;
  }

  if (errno != ENOENT) {
    a->retain();
    a->release();
    syscall_error("BPF_MAP_GET_NEXT_KEY");
  }

  return a;
}

auto Map::entries() -> pjs::Array* {
  if (!m_fd) return nullptr;

  uint8_t k[m_key_size];
  uint8_t v[m_value_size];

  auto a = pjs::Array::make();
  uint8_t *p = nullptr;

  try {
    union bpf_attr attr;
    while (!syscall_bpf(
      BPF_MAP_GET_NEXT_KEY, &attr, attr_size(next_key),
      [&](union bpf_attr &attr) {
        attr.map_fd = m_fd;
        attr.key = (uintptr_t)p;
        attr.next_key = (uintptr_t)k;
      }
    )) {
      if (syscall_bpf(
        BPF_MAP_LOOKUP_ELEM, &attr, attr_size(flags),
        [&](union bpf_attr &attr) {
          attr.map_fd = m_fd;
          attr.key = (uintptr_t)k;
          attr.value = (uintptr_t)v;
        }
      )) syscall_error("BPF_MAP_LOOKUP_ELEM");

      Data data_k(k, m_key_size, &s_dp);
      Data data_v(v, m_value_size, &s_dp);
      auto ent = pjs::Array::make(2);
      a->push(ent);

      if (m_key_type) {
        ent->set(0, m_key_type->decode(data_k));
      } else {
        ent->set(0, Data::make(std::move(data_k)));
      }

      if (m_value_type) {
        ent->set(1, m_value_type->decode(data_v));
      } else {
        ent->set(1, Data::make(std::move(data_v)));
      }

      p = k;
    }

    if (errno != ENOENT) syscall_error("BPF_MAP_GET_NEXT_KEY");

  } catch (std::runtime_error &) {
    a->retain();
    a->release();
    throw;
  }

  return a;
}

auto Map::lookup(pjs::Object *key) -> pjs::Object* {
  if (!key) return nullptr;

  pjs::Ref<Data> value;
  if (key->is<Data>()) {
    value = lookup_raw(key->as<Data>());
  } else if (m_key_type) {
    pjs::Ref<Data> raw_key = m_key_type->encode(key);
    value = lookup_raw(raw_key);
  }

  if (!value) return nullptr;
  if (m_value_type) {
    return m_value_type->decode(*value);
  } else {
    return value.release()->pass();
  }
}

void Map::update(pjs::Object *key, pjs::Object *value) {
  if (!key || !value) return;

  pjs::Ref<Data> k, v;

  if (key->is<Data>()) {
    k = key->as<Data>();
  } else if (m_key_type) {
    k = m_key_type->encode(key);
  }

  if (value->is<Data>()) {
    v = value->as<Data>();
  } else if (m_value_type) {
    v = m_value_type->encode(value);
  }

  update_raw(k, v);
}

void Map::remove(pjs::Object *key) {
  if (!key) return;
  if (key->is<Data>()) {
    delete_raw(key->as<Data>());
  } else if (m_key_type) {
    pjs::Ref<Data> k = m_key_type->encode(key);
    delete_raw(k);
  }
}

void Map::close() {
  ::close(m_fd);
  m_fd = 0;
}

auto Map::lookup_raw(Data *key) -> Data* {
  if (!m_fd) return nullptr;
  uint8_t k[m_key_size];
  uint8_t v[m_value_size];
  std::memset(k, 0, m_key_size);
  key->to_bytes(k, m_key_size);

  union bpf_attr attr;
  if (syscall_bpf(
    BPF_MAP_LOOKUP_ELEM, &attr, attr_size(flags),
    [&](union bpf_attr &attr) {
      attr.map_fd = m_fd;
      attr.key = (uintptr_t)k;
      attr.value = (uintptr_t)v;
    }
  )) syscall_error("BPF_MAP_LOOKUP_ELEM");

  return Data::make(&v[0], m_value_size, &s_dp);
}

void Map::update_raw(Data *key, Data *value) {
  if (!m_fd) return;
  uint8_t k[m_key_size];
  uint8_t v[m_value_size];
  std::memset(k, 0, m_key_size);
  std::memset(v, 0, m_value_size);
  key->to_bytes(k, m_key_size);
  value->to_bytes(v, m_value_size);

  union bpf_attr attr;
  if (syscall_bpf(
    BPF_MAP_UPDATE_ELEM, &attr, attr_size(flags),
    [&](union bpf_attr &attr) {
      attr.map_fd = m_fd;
      attr.key = (uintptr_t)k;
      attr.value = (uintptr_t)v;
    }
  )) syscall_error("BPF_MAP_UPDATE_ELEM");
}

void Map::delete_raw(Data *key) {
  if (!m_fd) return;
  uint8_t k[m_key_size];
  std::memset(k, 0, m_key_size);
  key->to_bytes(k, m_key_size);

  union bpf_attr attr;
  if (syscall_bpf(
    BPF_MAP_DELETE_ELEM, &attr, attr_size(flags),
    [&](union bpf_attr &attr) {
      attr.map_fd = m_fd;
      attr.key = (uintptr_t)k;
    }
  )) syscall_error("BPF_MAP_DELETE_ELEM");
}

#else // !PIPY_USE_BPF

static void unsupported() {
  throw std::runtime_error("BPF not supported");
}

ObjectFile::ObjectFile(Data *data) {
  unsupported();
}

auto Program::list() -> pjs::Array* {
  unsupported();
  return nullptr;
}

auto Program::maps() -> pjs::Array* {
  unsupported();
  return nullptr;
}

void Program::load() {
  unsupported();
}

auto Map::list() -> pjs::Array* {
  unsupported();
  return nullptr;
}

auto Map::open(int id, CStruct *key_type, CStruct *value_type) -> Map* {
  unsupported();
  return nullptr;
}

auto Map::keys() -> pjs::Array* {
  unsupported();
  return nullptr;
}

auto Map::entries() -> pjs::Array* {
  unsupported();
  return nullptr;
}

auto Map::lookup(pjs::Object *key) -> pjs::Object* {
  unsupported();
  return nullptr;
}

void Map::update(pjs::Object *key, pjs::Object *value) {
  unsupported();
}

void Map::remove(pjs::Object *key) {
  unsupported();
}

void Map::close() {
  unsupported();
}

#endif // PIPY_USE_BPF

} // namespace bpf
} // namespace pipy

namespace pjs {

using namespace pipy;
using namespace pipy::bpf;

//
// ObjectFile
//

template<> void ClassDef<ObjectFile>::init() {
  accessor("programs", [](Object *obj, Value &ret) {
    const auto &programs = obj->as<ObjectFile>()->programs;
    Array *a = Array::make(programs.size());
    for (size_t i = 0; i < programs.size(); i++) a->set(i, programs[i].get());
    ret.set(a);
  });

  accessor("maps", [](Object *obj, Value &ret) {
    const auto &maps = obj->as<ObjectFile>()->maps;
    Array *a = Array::make(maps.size());
    for (size_t i = 0; i < maps.size(); i++) a->set(i, maps[i].get());
    ret.set(a);
  });
}

//
// Program
//

template<> void ClassDef<Program>::init() {
  accessor("name", [](Object *obj, Value &ret) { ret.set(obj->as<Program>()->name()); });
  accessor("maps", [](Object *obj, Value &ret) { ret.set(obj->as<Program>()->maps()); });

  method("load", [](Context &ctx, Object *obj, Value &) {
    try {
      obj->as<Program>()->load();
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

template<> void ClassDef<Constructor<Program>>::init() {
  super<Function>();
  ctor();

  method("list", [](Context &ctx, Object *obj, Value &ret) {
    try {
      ret.set(Program::list());
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

//
// Map
//

template<> void ClassDef<bpf::Map>::init() {
  method("keys", [](Context &ctx, Object *obj, Value &ret) {
    try {
      ret.set(obj->as<bpf::Map>()->keys());
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("entries", [](Context &ctx, Object *obj, Value &ret) {
    try {
      ret.set(obj->as<bpf::Map>()->entries());
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("lookup", [](Context &ctx, Object *obj, Value &ret) {
    Object *key;
    if (!ctx.arguments(1, &key)) return;
    try {
      ret.set(obj->as<bpf::Map>()->lookup(key));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("update", [](Context &ctx, Object *obj, Value &ret) {
    Object *key, *value;
    if (!ctx.arguments(2, &key, &value)) return;
    try {
      obj->as<bpf::Map>()->update(key, value);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("delete", [](Context &ctx, Object *obj, Value &ret) {
    Object *key;
    if (!ctx.arguments(1, &key)) return;
    try {
      obj->as<bpf::Map>()->remove(key);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

template<> void ClassDef<Constructor<bpf::Map>>::init() {
  super<Function>();
  ctor();

  method("list", [](Context &ctx, Object *obj, Value &ret) {
    try {
      ret.set(bpf::Map::list());
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("open", [](Context &ctx, Object *obj, Value &ret) {
    int id;
    CStruct *key_type = nullptr;
    CStruct *value_type = nullptr;
    if (!ctx.arguments(1, &id, &key_type, &value_type)) return;
    try {
      ret.set(bpf::Map::open(id, key_type, value_type));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

template<> void ClassDef<BPF>::init() {
  ctor();

  method("object", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    if (!ctx.arguments(1, &data)) return;
    try {
      ret.set(ObjectFile::make(data));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  variable("Program", class_of<Constructor<Program>>());
  variable("Map", class_of<Constructor<bpf::Map>>());
}

//
// Map::Info
//

template<> void ClassDef<bpf::Map::Info>::init() {
  field<Ref<Str>>("name", [](bpf::Map::Info *obj) { return &obj->name; });
  field<int>("id", [](bpf::Map::Info *obj) { return &obj->id; });
  field<int>("flags", [](bpf::Map::Info *obj) { return &obj->flags; });
  field<int>("maxEntries", [](bpf::Map::Info *obj) { return &obj->maxEntries; });
  field<int>("keySize", [](bpf::Map::Info *obj) { return &obj->keySize; });
  field<int>("valueSize", [](bpf::Map::Info *obj) { return &obj->valueSize; });
}

} // namespace pjs
