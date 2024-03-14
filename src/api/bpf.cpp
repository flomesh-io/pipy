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

#include <fcntl.h>
#include <unistd.h>
#include <elf.h>
#include <sys/syscall.h>
#include <linux/bpf.h>
#include <linux/btf.h>

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

  for (size_t i = 0; i < obj.sections.size(); i++) {
    if (obj.sections[i].name == ".BTF") {
      BTF btf(obj, i);
      find_maps(btf);
      break;
    }
  }

  for (size_t i = 0; i < obj.symbols.size(); i++) {
    const auto &sym = obj.symbols[i];
    auto sec_idx = sym.shndx;
    if (sym.type != STT_FUNC) continue;
    if (sec_idx >= obj.sections.size()) continue;
    const auto &sec = obj.sections[sec_idx];
    if (sec.type != SHT_PROGBITS) continue;
    if (!(sec.flags & SHF_EXECINSTR)) continue;
    auto offset = sym.value;
    auto length = sym.size;
    if (offset + length > sec.size) continue;

    std::vector<uint8_t> insts(sec.data + offset, sec.data + offset + length);
    std::vector<Program::Reloc> relocs;

    for (const auto &reloc : obj.relocations) {
      if (reloc.section != sec_idx) continue;
      for (const auto &ent : reloc.entries) {
        if (ent.offset < offset) continue;
        if (ent.offset >= offset + length) continue;
        if (ent.sym < 0 || ent.sym >= obj.symbols.size()) {
          throw std::runtime_error(
            "ELF symbol index out of range: " + std::to_string(ent.sym)
          );
        }
        const auto &name = obj.symbols[ent.sym].name;
        Map *map = nullptr;
        for (const auto &m : maps) {
          if (m->name()->str() == name) {
            map = m.get();
            break;
          }
        }
        if (!map) throw std::runtime_error("map not found: " + name);
        int pos = (ent.offset - offset) / sizeof(struct bpf_insn);
        relocs.push_back(Program::Reloc{ pos, map });
      }
    }

    programs.push_back(Program::make(sym.name, insts, relocs));
  }
}

void ObjectFile::find_maps(const BTF &btf) {
  const auto get_meta_int = [&](size_t type) {
    auto ptr_type = find_type(btf, type);
    if (ptr_type->kind != BTF_KIND_PTR) {
      throw std::runtime_error(
        "BTF type " + std::to_string(type) + " of kind BTF_KIND_PTR expected"
      );
    }
    auto array_type = find_type(btf, ptr_type->type);
    if (array_type->kind != BTF_KIND_ARRAY) {
      throw std::runtime_error(
        "BTF type " + std::to_string(ptr_type->type) + " of kind BTF_KIND_ARRAY expected"
      );
    }
    return static_cast<const BTF::Array*>(array_type)->nelems;
  };

  const auto get_meta_type = [&](size_t type) {
    auto ptr_type = find_type(btf, type);
    if (ptr_type->kind != BTF_KIND_PTR) {
      throw std::runtime_error(
        "BTF type " + std::to_string(type) + " of kind BTF_KIND_PTR expected"
      );
    }
    return ptr_type->type;
  };

  for (const auto &pt : btf.types) {
    if (!pt) continue;
    if (pt->kind != BTF_KIND_DATASEC) continue;
    if (pt->name != ".maps") continue;
    const auto &sec = *static_cast<BTF::DataSec*>(pt.get());
    for (const auto &v : sec.vars) {
      auto meta_var = find_type(btf, v.type);
      auto meta_type = find_type(btf, meta_var->type);
      if (meta_var->kind != BTF_KIND_VAR) continue;
      if (meta_type->kind != BTF_KIND_STRUCT) continue;
      const auto &name = meta_var->name;
      const auto &meta = *static_cast<const BTF::Struct*>(meta_type);
      int map_type = -1;
      int map_flags = 0;
      int max_entries = -1;
      int key_type = -1;
      int value_type = -1;
      for (const auto &m : meta.members) {
        if (m.name == "type") map_type = get_meta_int(m.type);
        else if (m.name == "max_entries") max_entries = get_meta_int(m.type);
        else if (m.name == "map_flags") map_flags = get_meta_int(m.type);
        else if (m.name == "key") key_type = m.type;
        else if (m.name == "value") value_type = m.type;
      }
      if (map_type < 0) throw std::runtime_error("type missing for " + name);
      if (max_entries < 0) throw std::runtime_error("max_entries missing for " + name);
      if (key_type < 0) throw std::runtime_error("key missing for " + name);
      if (value_type < 0) throw std::runtime_error("value missing for " + name);
      Log::debug(
        Log::BPF, "[bpf] found map '%s' type %d flags %d max_entries %d key %d type %d",
        name.c_str(), map_type, map_flags, max_entries, key_type, value_type
      );
      key_type = get_meta_type(key_type);
      value_type = get_meta_type(value_type);
      maps.push_back(
        Map::make(
          name, map_type, map_flags, max_entries,
          make_struct(btf, key_type),
          make_struct(btf, value_type)
        )
      );
    }
  }
}

auto ObjectFile::find_type(const BTF &btf, size_t type) -> const BTF::Type* {
  const BTF::Type *t = nullptr;
  for (;;) {
    if (!type || type >= btf.types.size()) {
      throw std::runtime_error(
        "BTF type id out of range: " + std::to_string(type)
      );
    }
    t = btf.types[type].get();
    if (t->kind != BTF_KIND_TYPEDEF) return t;
    type = t->type;
  }
}

auto ObjectFile::make_struct(const BTF &btf, size_t type) -> CStructBase* {
  thread_local static pjs::ConstStr s_i("i");

  const auto int_type = [&](const BTF::Int *t) -> const char * {
    if (t->is_char && t->bits == 8) {
      return "char";
    } else {
      switch (t->bits) {
        case 8: return t->is_signed ? "int8" : "uint8";
        case 16: return t->is_signed ? "int16" : "uint16";
        case 32: return t->is_signed ? "int32" : "uint32";
        case 64: return t->is_signed ? "int64" : "uint64";
        default: throw std::runtime_error("unsupported integer bitwidth " + std::to_string(t->bits));
      }
    }
  };

  const auto unsupported_type = [](size_t type) {
    throw std::runtime_error("unsupported BTF type for maps: " + std::to_string(type));
  };

  const auto *t = find_type(btf, type);
  switch (t->kind) {
    case BTF_KIND_STRUCT:
    case BTF_KIND_UNION: {
      auto cs = (t->kind == BTF_KIND_UNION
        ? (CStructBase *)CUnion::make()
        : (CStructBase *)CStruct::make()
      );
      try {
        for (const auto &m : static_cast<const BTF::Struct*>(t)->members) {
          const auto *mt = find_type(btf, m.type);
          bool is_array = false;
          int array_size = 0;
          if (mt->kind == BTF_KIND_ARRAY) {
            auto at = static_cast<const BTF::Array*>(mt);
            is_array = true;
            array_size = at->nelems;
            mt = find_type(btf, at->elem_type);
            if (mt->kind == BTF_KIND_ARRAY) {
              throw std::runtime_error("multi-dimensional array not supported");
            }
          }
          switch (mt->kind) {
            case BTF_KIND_STRUCT:
            case BTF_KIND_UNION: {
              if (is_array) throw std::runtime_error("array of structs or unions not supported");
              cs->add_field(pjs::Str::make(m.name), make_struct(btf, m.type));
              break;
            }
            case BTF_KIND_INT: {
              auto *it = int_type(static_cast<const BTF::Int*>(mt));
              if (is_array) {
                char s[100];
                std::snprintf(s, sizeof(s), "%s[%d]", it, array_size);
                cs->add_field(pjs::Str::make(m.name), s);
              } else {
                cs->add_field(pjs::Str::make(m.name), it);
              }
              break;
            }
            default: unsupported_type(m.type);
          }
        }
      } catch (std::runtime_error &) {
        cs->retain();
        cs->release();
        throw;
      }
      return cs;
    }
    case BTF_KIND_INT: {
      auto cs = CStruct::make();
      cs->add_field(s_i, int_type(static_cast<const BTF::Int*>(t)));
      return cs;
    }
    default: unsupported_type(type); return nullptr;
  }
}

//
// Program
//

Program::Program(const std::string &name, std::vector<uint8_t> &insts, std::vector<Reloc> &relocs)
  : m_name(pjs::Str::make(name))
  , m_insts(std::move(insts))
  , m_relocs(std::move(relocs))
{
}

auto Program::list() -> pjs::Array* {
  return nullptr;
}

auto Program::size() const -> int {
  return m_insts.size() / sizeof(struct bpf_insn);
}

void Program::load(int type, const std::string &license) {
  if (m_fd) return;

  std::vector<struct bpf_insn> insts(size());
  std::memcpy(insts.data(), m_insts.data(), insts.size() * sizeof(insts[0]));
  for (const auto &reloc : m_relocs) {
    auto &i = insts[reloc.position];
    auto *m = reloc.map.get();
    if (!m->fd()) m->create();
    i.src_reg = BPF_PSEUDO_MAP_FD;
    i.imm = m->fd();
  }

  std::vector<char> log_buf(100*1024);
  union bpf_attr attr;
  int fd = syscall_bpf(
    BPF_PROG_LOAD, &attr, attr_size(line_info_cnt),
    [&](union bpf_attr &attr) {
      std::strncpy(attr.prog_name, m_name->c_str(), sizeof(attr.prog_name));
      attr.prog_type = type;
      attr.insn_cnt = insts.size();
      attr.insns = (uintptr_t)insts.data();
      attr.license = (uintptr_t)license.c_str();
      attr.log_level = 1;
      attr.log_size = log_buf.size();
      attr.log_buf = (uintptr_t)log_buf.data();
    }
  );
  if (log_buf[0] && Log::is_enabled(Log::BPF)) {
    Log::debug(Log::BPF, "[bpf] In-kernel verifier log:");
    Log::write(log_buf.data());
  }
  if (fd < 0) syscall_error("BPF_PROG_LOAD");

  struct bpf_prog_info info = {};
  if (syscall_bpf(
    BPF_OBJ_GET_INFO_BY_FD, &attr, attr_size(info),
    [&](union bpf_attr &attr) {
      attr.info.bpf_fd = fd;
      attr.info.info_len = sizeof(info);
      attr.info.info = (uint64_t)&info;
    }
  )) syscall_error("BPF_OBJ_GET_INFO_BY_FD");

  m_fd = fd;
  m_id = info.id;
}

//
// Map
//

Map::Map(const std::string &name, int type, int flags, int max_entries, int key_size, int value_size)
  : m_name(pjs::Str::make(name))
  , m_fd(0)
  , m_id(0)
  , m_type(type)
  , m_flags(flags)
  , m_max_entries(max_entries)
  , m_key_size(key_size)
  , m_value_size(value_size)
{
}

Map::Map(const std::string &name, int type, int flags, int max_entries, CStructBase *key_type, CStructBase *value_type)
  : m_name(pjs::Str::make(name))
  , m_fd(0)
  , m_id(0)
  , m_type(type)
  , m_flags(flags)
  , m_max_entries(max_entries)
  , m_key_size(key_type ? key_type->size() : 0)
  , m_value_size(value_type ? value_type->size() : 0)
  , m_key_type(key_type)
  , m_value_type(value_type)
{
}

Map::Map(int fd, CStructBase *key_type, CStructBase *value_type)
  : m_fd(fd)
  , m_key_type(key_type)
  , m_value_type(value_type)
{
  union bpf_attr attr;
  struct bpf_map_info info = {};
  if (syscall_bpf(
    BPF_OBJ_GET_INFO_BY_FD, &attr, attr_size(info),
    [&](union bpf_attr &attr) {
      attr.info.bpf_fd = fd;
      attr.info.info_len = sizeof(info);
      attr.info.info = (uint64_t)&info;
    }
  )) syscall_error("BPF_OBJ_GET_INFO_BY_FD");

  m_name = pjs::Str::make(info.name);
  m_id = info.id;
  m_type = info.type;
  m_flags = info.map_flags;
  m_max_entries = info.max_entries;
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
      i->type = info.type;
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

auto Map::open(int id, CStructBase *key_type, CStructBase *value_type) -> Map* {
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

void Map::create() {
  if (m_fd) return;

  union bpf_attr attr;
  int fd = syscall_bpf(
    BPF_MAP_CREATE, &attr, attr_size(btf_value_type_id),
    [&](union bpf_attr &attr) {
      std::strncpy(attr.map_name, m_name->c_str(), sizeof(attr.map_name));
      attr.map_type = m_type;
      attr.key_size = m_key_size;
      attr.value_size = m_value_size;
      attr.max_entries = m_max_entries;
      attr.map_flags = m_flags;
    }
  );
  if (fd < 0) syscall_error("BPF_MAP_CREATE");

  struct bpf_map_info info = {};
  if (syscall_bpf(
    BPF_OBJ_GET_INFO_BY_FD, &attr, attr_size(info),
    [&](union bpf_attr &attr) {
      attr.info.bpf_fd = fd;
      attr.info.info_len = sizeof(info);
      attr.info.info = (uint64_t)&info;
    }
  )) syscall_error("BPF_OBJ_GET_INFO_BY_FD");

  m_fd = fd;
  m_id = info.id;
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

//
// BPF
//

void BPF::pin(const std::string &pathname, int fd) {
  union bpf_attr attr;
  if (syscall_bpf(
    BPF_OBJ_PIN, &attr, attr_size(file_flags),
    [&](union bpf_attr &attr) {
      attr.pathname = (uintptr_t)pathname.c_str();
      attr.bpf_fd = fd;
    }
  )) syscall_error("BPF_OBJ_PIN");
}

static void bpf_prog_attach(int attach_type, int fd, int target_fd = 0) {
  union bpf_attr attr;
  if (syscall_bpf(
    BPF_PROG_ATTACH, &attr, attr_size(attach_flags),
    [&](union bpf_attr &attr) {
      attr.target_fd = target_fd;
      attr.attach_bpf_fd = fd;
      attr.attach_type = attach_type;
    }
  )) syscall_error("BPF_PROG_ATTACH");
}

static void bpf_prog_detach(int attach_type, int fd, int target_fd = 0) {
  union bpf_attr attr;
  if (syscall_bpf(
    BPF_PROG_DETACH, &attr, attr_size(attach_flags),
    [&](union bpf_attr &attr) {
      attr.target_fd = target_fd;
      attr.attach_bpf_fd = fd;
      attr.attach_type = attach_type;
    }
  )) syscall_error("BPF_PROG_DETACH");
}

void BPF::attach(int attach_type, int fd) {
  bpf_prog_attach(attach_type, fd);
}

void BPF::detach(int attach_type, int fd) {
  bpf_prog_detach(attach_type, fd);
}

void BPF::attach(int attach_type, int fd, const std::string &cgroup) {
  auto target_fd = open(cgroup.c_str(), O_RDONLY);
  if (target_fd < 0) {
    throw std::runtime_error("Cannot open cgroup " + cgroup);
  }
  bpf_prog_attach(attach_type, fd, target_fd);
  close(target_fd);
}

void BPF::detach(int attach_type, int fd, const std::string &cgroup) {
  auto target_fd = open(cgroup.c_str(), O_RDONLY);
  if (target_fd < 0) {
    throw std::runtime_error("Cannot open cgroup " + cgroup);
  }
  bpf_prog_detach(attach_type, fd, target_fd);
  close(target_fd);
}

void BPF::attach(int attach_type, int fd, int map_fd) {
  bpf_prog_attach(attach_type, fd, map_fd);
}

void BPF::detach(int attach_type, int fd, int map_fd) {
  bpf_prog_detach(attach_type, fd, map_fd);
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

auto Program::size() const -> int {
  return 0;
}

void Program::load(int type, const std::string &license) {
  unsupported();
}

auto Map::list() -> pjs::Array* {
  unsupported();
  return nullptr;
}

void Map::create() {
  unsupported();
}

auto Map::open(int id, CStructBase *key_type, CStructBase *value_type) -> Map* {
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

void BPF::pin(const std::string &pathname, int fd) {
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
  accessor("size", [](Object *obj, Value &ret) { ret.set(obj->as<Program>()->size()); });
  accessor("fd", [](Object *obj, Value &ret) { ret.set(obj->as<Program>()->fd()); });
  accessor("id", [](Object *obj, Value &ret) { ret.set(obj->as<Program>()->id()); });

  method("load", [](Context &ctx, Object *obj, Value &ret) {
    int type;
    Str* license;
    if (!ctx.arguments(2, &type, &license)) return;
    try {
      obj->as<Program>()->load(type, license->str());
      ret.set(obj);
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
  accessor("name", [](Object *obj, Value &ret) { ret.set(obj->as<bpf::Map>()->name()); });
  accessor("fd", [](Object *obj, Value &ret) { ret.set(obj->as<bpf::Map>()->fd()); });
  accessor("id", [](Object *obj, Value &ret) { ret.set(obj->as<bpf::Map>()->id()); });
  accessor("type", [](Object *obj, Value &ret) { ret.set(obj->as<bpf::Map>()->type()); });
  accessor("flags", [](Object *obj, Value &ret) { ret.set(obj->as<bpf::Map>()->flags()); });
  accessor("maxEntries", [](Object *obj, Value &ret) { ret.set(obj->as<bpf::Map>()->max_entries()); });
  accessor("keySize", [](Object *obj, Value &ret) { ret.set(obj->as<bpf::Map>()->key_size()); });
  accessor("keyType", [](Object *obj, Value &ret) { ret.set(obj->as<bpf::Map>()->key_type()); });
  accessor("valueSize", [](Object *obj, Value &ret) { ret.set(obj->as<bpf::Map>()->value_size()); });
  accessor("valueType", [](Object *obj, Value &ret) { ret.set(obj->as<bpf::Map>()->value_type()); });

  method("create", [](Context &ctx, Object *obj, Value &ret) {
    try {
      obj->as<bpf::Map>()->create();
      ret.set(obj);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

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
    CStructBase *key_type = nullptr;
    CStructBase *value_type = nullptr;
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
    if (!data) { return ctx.error_argument_type(0, "non-null Data"); }
    try {
      ret.set(ObjectFile::make(data));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("pin", [](Context &ctx, Object *obj, Value &ret) {
    Str *pathname;
    int fd;
    try {
      if (!ctx.arguments(2, &pathname, &fd)) return;
      BPF::pin(pathname->str(), fd);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("attach", [](Context &ctx, Object *obj, Value &ret) {
    int attach_type;
    int fd;
    int target_fd;
    Str *cgroup;
    try {
      if (!ctx.check(0, attach_type)) return;
      if (!ctx.check(1, fd)) return;
      if (ctx.argc() == 2) {
        BPF::attach(attach_type, fd);
      } else if (ctx.get(2, cgroup)) {
        BPF::attach(attach_type, fd, cgroup->str());
      } else if (ctx.get(2, target_fd)) {
        BPF::attach(attach_type, fd, target_fd);
      } else {
        return ctx.error_argument_type(2, "a number or a string");
      }
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("detach", [](Context &ctx, Object *obj, Value &ret) {
    int attach_type;
    int fd;
    int target_fd;
    Str *cgroup;
    try {
      if (!ctx.check(0, attach_type)) return;
      if (!ctx.check(1, fd)) return;
      if (ctx.argc() == 2) {
        BPF::detach(attach_type, fd);
      } else if (ctx.get(2, cgroup)) {
        BPF::detach(attach_type, fd, cgroup->str());
      } else if (ctx.get(2, target_fd)) {
        BPF::detach(attach_type, fd, target_fd);
      } else {
        return ctx.error_argument_type(2, "a number or a string");
      }
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
