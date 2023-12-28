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
#include "log.hpp"

#ifdef __linux__
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/bpf.h>
#include <elf.h>

#define attr_size(FIELD) \
  (offsetof(bpf_attr, FIELD) + sizeof(bpf_attr::FIELD))

#endif // __linux__

namespace pipy {
namespace bpf {

thread_local static Data::Producer s_dp("BPF");

#ifdef __linux__

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

//
// OBJ
//

class OBJ {
public:

  //
  // OBJ::Section
  //

  struct Section {
    std::string name;
    int type;
    int flags;
    const uint8_t *data;
    size_t size;
    size_t addr;
    size_t addralign;
    int link;
    int info;
  };

  //
  // OBJ::Symbol
  //

  struct Symbol {
    std::string name;
    size_t value;
    size_t size;
    size_t shndx;
    int info;
  };

  //
  // OBJ::Relocation
  //

  struct Relocation {

    //
    // OBJ::Relocation::Entry
    //

    struct Entry {
      size_t offset;
      uint64_t info;
    };

    size_t section;
    std::vector<Entry> entries;
  };

  int type;
  int flags;
  int machine;
  int version;
  size_t entry;
  std::vector<Section> sections;
  std::vector<Symbol> symbols;
  std::vector<Relocation> relocations;

  OBJ(const Data &elf) {
    elf.to_bytes(m_elf);

    if (m_elf.size() <= EI_NIDENT ||
      m_elf[EI_MAG0] != ELFMAG0 ||
      m_elf[EI_MAG1] != ELFMAG1 ||
      m_elf[EI_MAG2] != ELFMAG2 ||
      m_elf[EI_MAG3] != ELFMAG3 ||
      m_elf[EI_CLASS] <= ELFCLASSNONE ||
      m_elf[EI_CLASS] >= ELFCLASSNUM ||
      m_elf[EI_VERSION] != EV_CURRENT
    ) throw std::runtime_error("not an ELF file");

    if (
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      m_elf[EI_DATA] != ELFDATA2LSB
#else
      m_elf[EI_DATA] != ELFDATA2MSB
#endif
    ) throw std::runtime_error("mismatched ELF endianness");

    if (m_elf[EI_OSABI] != ELFOSABI_SYSV) {
      throw std::runtime_error("unsupported ABI");
    }

    auto cls = m_elf[EI_CLASS];

    size_t phoff, shoff, ehsize, shstrndx;
    size_t phentsize, phnum;
    size_t shentsize, shnum;

    (void)ehsize;
    (void)phoff;
    (void)phentsize;
    (void)phnum;

    switch (cls) {
      case ELFCLASS32: {
        auto &hdr = *(Elf32_Ehdr *)m_elf.data();
        type = hdr.e_type;
        machine = hdr.e_machine;
        version = hdr.e_version;
        entry = hdr.e_entry;
        phoff = hdr.e_phoff;
        shoff = hdr.e_shoff;
        flags = hdr.e_flags;
        ehsize = hdr.e_ehsize;
        phentsize = hdr.e_phentsize;
        phnum = hdr.e_phnum;
        shentsize = hdr.e_shentsize;
        shnum = hdr.e_shnum;
        shstrndx = hdr.e_shstrndx;
        break;
      }
      case ELFCLASS64: {
        auto &hdr = *(Elf64_Ehdr *)m_elf.data();
        type = hdr.e_type;
        machine = hdr.e_machine;
        version = hdr.e_version;
        entry = hdr.e_entry;
        phoff = hdr.e_phoff;
        shoff = hdr.e_shoff;
        flags = hdr.e_flags;
        ehsize = hdr.e_ehsize;
        phentsize = hdr.e_phentsize;
        phnum = hdr.e_phnum;
        shentsize = hdr.e_shentsize;
        shnum = hdr.e_shnum;
        shstrndx = hdr.e_shstrndx;
        break;
      }
      default: throw std::runtime_error("unsupported ELF file class");
    }

    if (shoff + shentsize * shnum > m_elf.size() || shstrndx >= shnum) {
      std::runtime_error("offset out of ELF file boundary");
    }

    std::vector<size_t> name_offsets(shnum);
    sections.resize(shnum);

    for (size_t i = 0; i < shnum; i++) {
      auto offset = shoff + shentsize * i;
      auto &sec = sections[i];
      switch (cls) {
        case ELFCLASS32: {
          auto &hdr = *(Elf32_Shdr *)(m_elf.data() + offset);
          if (hdr.sh_offset + hdr.sh_size > m_elf.size()) {
            section_out_of_bound(i);
          }
          name_offsets[i] = hdr.sh_name;
          sec.type = hdr.sh_type;
          sec.flags = hdr.sh_flags;
          sec.addr = hdr.sh_addr;
          sec.data = m_elf.data() + hdr.sh_offset;
          sec.size = hdr.sh_size;
          sec.link = hdr.sh_link;
          sec.info = hdr.sh_info;
          sec.addralign = hdr.sh_addralign;
          break;
        }
        case ELFCLASS64: {
          auto &hdr = *(Elf64_Shdr *)(m_elf.data() + offset);
          if (hdr.sh_offset + hdr.sh_size > m_elf.size()) {
            section_out_of_bound(i);
          }
          name_offsets[i] = hdr.sh_name;
          sec.type = hdr.sh_type;
          sec.flags = hdr.sh_flags;
          sec.addr = hdr.sh_addr;
          sec.data = m_elf.data() + hdr.sh_offset;
          sec.size = hdr.sh_size;
          sec.link = hdr.sh_link;
          sec.info = hdr.sh_info;
          sec.addralign = hdr.sh_addralign;
          break;
        }
      }
    }

    auto &str_tab_sec = sections[shstrndx];
    auto str_tab_head = str_tab_sec.data;
    auto str_tab_size = sections[shstrndx].size;

    auto find_str = [&](size_t offset, std::string &str) {
      if (offset >= str_tab_size) return false;
      auto end = offset;
      while (end < str_tab_size && str_tab_head[end]) end++;
      str = std::string((const char *)(str_tab_head + offset), end - offset);
      return true;
    };

    for (size_t i = 0; i < shnum; i++) {
      auto &s = sections[i];
      if (!find_str(name_offsets[i], s.name)) {
        section_out_of_bound(i);
      }
      Log::debug(Log::BPF,
        "[bpf] SECTION #%d name '%s' addr 0x%08x size %d type %d flags %d link %d info %d",
        int(i), s.name.c_str(), int(s.addr), int(s.size), s.type, s.flags, s.link, s.info
      );
    }

    for (const auto &sec : sections) {
      if (sec.type == SHT_SYMTAB) {
        size_t entry_size = 0;
        switch (cls) {
          case ELFCLASS32: entry_size = sizeof(Elf32_Sym); break;
          case ELFCLASS64: entry_size = sizeof(Elf64_Sym); break;
        }
        size_t n = entry_size ? sec.size / entry_size : 0;
        symbols.resize(n);
        for (size_t i = 0; i < n; i++) {
          auto &s = symbols[i];
          auto offset = entry_size * i;
          switch (cls) {
            case ELFCLASS32: {
              const auto &ent = *(Elf32_Sym *)(sec.data + offset);
              if (!find_str(ent.st_name, s.name)) {
                symbol_out_of_bound(i);
              }
              s.value = ent.st_value;
              s.size = ent.st_size;
              s.shndx = ent.st_shndx;
              s.info = ent.st_info;
              break;
            }
            case ELFCLASS64: {
              const auto &ent = *(Elf64_Sym *)(sec.data + offset);
              if (!find_str(ent.st_name, s.name)) {
                symbol_out_of_bound(i);
              }
              s.value = ent.st_value;
              s.size = ent.st_size;
              s.shndx = ent.st_shndx;
              s.info = ent.st_info;
              break;
            }
          }
          Log::debug(Log::BPF,
            "[bpf] SYMBOL #%d name '%s' value %d size %d shndx %d info %d",
            int(i), s.name.c_str(), int(s.value), int(s.shndx), s.info
          );
        }
        break;
      }
    }

    for (size_t i = 0; i < sections.size(); i++) {
      const auto &sec = sections[i];
      if (sec.type == SHT_REL) {
        size_t entry_size = 0;
        switch (cls) {
          case ELFCLASS32: entry_size = sizeof(Elf32_Rel); break;
          case ELFCLASS64: entry_size = sizeof(Elf64_Rel); break;
        }
        size_t n = entry_size ? sec.size / entry_size : 0;
        relocations.emplace_back();
        auto &reloc = relocations.back();
        reloc.section = sec.info;
        reloc.entries.resize(n);
        if (sec.info >= sections.size()) {
          relocation_out_of_bound(i);
        }
        auto max_offset = sections[sec.info].size;
        for (size_t j = 0; j < n; j++) {
          auto &r = reloc.entries[j];
          auto offset = entry_size * j;
          switch (cls) {
            case ELFCLASS32: {
              const auto &ent = *(Elf32_Rel *)(sec.data + offset);
              r.offset = ent.r_offset;
              r.info = ent.r_info;
              break;
            }
            case ELFCLASS64: {
              const auto &ent = *(Elf64_Rel *)(sec.data + offset);
              r.offset = ent.r_offset;
              r.info = ent.r_info;
              break;
            }
          }
          Log::debug(Log::BPF,
            "[bpf] RELOC for SECTION #%d offset %d info %llu",
            int(reloc.section), int(r.offset), r.info
          );
          if (r.offset >= max_offset) {
            relocation_out_of_bound(i);
          }
        }
      }
    }
  }

private:
  std::vector<uint8_t> m_elf;

  static void section_out_of_bound(size_t i) {
    std::string msg("out of bound section: index = ");
    msg += std::to_string(i);
    throw std::runtime_error(msg);
  }

  static void symbol_out_of_bound(size_t i) {
    std::string msg("out of bound symbol: index = ");
    msg += std::to_string(i);
    throw std::runtime_error(msg);
  }

  static void relocation_out_of_bound(size_t i) {
    std::string msg("out of bound relocation: index = ");
    msg += std::to_string(i);
    throw std::runtime_error(msg);
  }
};

#endif // __linux__

//
// Program
//

auto Program::list() -> pjs::Array* {
  return nullptr;
}

auto Program::load(Data *elf) -> Program* {
  Program *prog = nullptr;

#ifdef __linux__
  OBJ obj(*elf);
#endif

  return prog;
}

auto Program::maps() -> pjs::Array* {
  return nullptr;
}

//
// Map
//

Map::Map(int fd, CStruct *key_type, CStruct *value_type)
  : m_fd(fd)
  , m_key_type(key_type)
  , m_value_type(value_type)
{
#ifdef __linux__
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
#endif // __linux__
}

auto Map::list() -> pjs::Array* {
  auto a = pjs::Array::make();
#ifdef __linux__
  union bpf_attr attr;
  unsigned int id = 0;
  for (;;) {
    if (!syscall_bpf(
      BPF_MAP_GET_NEXT_ID, &attr, attr_size(open_flags),
      [&](union bpf_attr &attr) { attr.start_id = id; }
    )) {
      id = attr.next_id;
    } else {
      break;
    }

    int fd = syscall_bpf(
      BPF_MAP_GET_FD_BY_ID, &attr, attr_size(open_flags),
      [&](union bpf_attr &attr) { attr.map_id = id; }
    );

		struct bpf_map_info info = {};
    syscall_bpf(
      BPF_OBJ_GET_INFO_BY_FD, &attr, attr_size(info),
      [&](union bpf_attr &attr) {
        attr.info.bpf_fd = fd;
        attr.info.info_len = sizeof(info);
        attr.info.info = (uint64_t)&info;
      }
    );
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
#endif // __linux__
  return a;
}

auto Map::open(int id, CStruct *key_type, CStruct *value_type) -> Map* {
#ifdef __linux__
  union bpf_attr attr;
  int fd = syscall_bpf(
    BPF_MAP_GET_FD_BY_ID, &attr, attr_size(open_flags),
    [&](union bpf_attr &attr) { attr.map_id = id; }
  );
  if (fd <= 0) {
    throw std::runtime_error("failed when trying to get fd by a map id");
  }
  return Map::make(fd, key_type, value_type);
#else
  return nullptr;
#endif // __linux__
}

auto Map::keys() -> pjs::Array* {
  if (!m_fd) return nullptr;
#ifdef __linux__
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

  return a;
#else
  return nullptr;
#endif // __linux__
}

auto Map::entries() -> pjs::Array* {
  if (!m_fd) return nullptr;
#ifdef __linux__
  uint8_t k[m_key_size];
  uint8_t v[m_value_size];

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
    if (syscall_bpf(
      BPF_MAP_LOOKUP_ELEM, &attr, attr_size(flags),
      [&](union bpf_attr &attr) {
        attr.map_fd = m_fd;
        attr.key = (uintptr_t)k;
        attr.value = (uintptr_t)v;
      }
    )) break;

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

  return a;
#else
  return nullptr;
#endif // __linux__
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
#ifdef __linux__
  ::close(m_fd);
#endif
  m_fd = 0;
}

auto Map::lookup_raw(Data *key) -> Data* {
  if (!m_fd) return nullptr;
#ifdef __linux__
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
  )) return nullptr;

  return Data::make(&v[0], m_value_size, &s_dp);
#else
  return nullptr;
#endif // __linux__
}

void Map::update_raw(Data *key, Data *value) {
  if (!m_fd) return;
#ifdef __linux__
  uint8_t k[m_key_size];
  uint8_t v[m_value_size];
  std::memset(k, 0, m_key_size);
  std::memset(v, 0, m_value_size);
  key->to_bytes(k, m_key_size);
  value->to_bytes(v, m_value_size);

  union bpf_attr attr;
  syscall_bpf(
    BPF_MAP_UPDATE_ELEM, &attr, attr_size(flags),
    [&](union bpf_attr &attr) {
      attr.map_fd = m_fd;
      attr.key = (uintptr_t)k;
      attr.value = (uintptr_t)v;
    }
  );
#endif // __linux__
}

void Map::delete_raw(Data *key) {
  if (!m_fd) return;
#ifdef __linux__
  uint8_t k[m_key_size];
  std::memset(k, 0, m_key_size);
  key->to_bytes(k, m_key_size);

  union bpf_attr attr;
  syscall_bpf(
    BPF_MAP_DELETE_ELEM, &attr, attr_size(flags),
    [&](union bpf_attr &attr) {
      attr.map_fd = m_fd;
      attr.key = (uintptr_t)k;
    }
  );
#endif // __linux__
}

} // namespace bpf
} // namespace pipy

namespace pjs {

using namespace pipy;
using namespace pipy::bpf;

static bool linux_only(Context &ctx) {
#ifdef __linux__
  return true;
#else
  ctx.error("BPF not supported");
  return false;
#endif
}

//
// Program
//

template<> void ClassDef<Program>::init() {
  accessor("maps", [](Object *obj, Value &ret) { ret.set(obj->as<Program>()); });
}

template<> void ClassDef<Constructor<Program>>::init() {
  super<Function>();
  ctor();

  method("list", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      ret.set(Program::list());
    }
  });

  method("load", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      pipy::Data *data;
      if (!ctx.arguments(1, &data)) return;
      try {
        ret.set(Program::load(data));
      } catch (std::runtime_error &err) {
        ctx.error(err);
      }
    }
  });
}

//
// Map
//

template<> void ClassDef<bpf::Map>::init() {
  method("keys", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      ret.set(obj->as<bpf::Map>()->keys());
    }
  });

  method("entries", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      ret.set(obj->as<bpf::Map>()->entries());
    }
  });

  method("lookup", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      Object *key;
      if (!ctx.arguments(1, &key)) return;
      ret.set(obj->as<bpf::Map>()->lookup(key));
    }
  });

  method("update", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      Object *key, *value;
      if (!ctx.arguments(2, &key, &value)) return;
      obj->as<bpf::Map>()->update(key, value);
    }
  });

  method("delete", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      Object *key;
      if (!ctx.arguments(1, &key)) return;
      obj->as<bpf::Map>()->remove(key);
    }
  });
}

template<> void ClassDef<Constructor<bpf::Map>>::init() {
  super<Function>();
  ctor();

  method("list", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      ret.set(bpf::Map::list());
    }
  });

  method("open", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      int id;
      CStruct *key_type = nullptr;
      CStruct *value_type = nullptr;
      if (!ctx.arguments(1, &id, &key_type, &value_type)) return;
      try {
        ret.set(bpf::Map::open(id, key_type, value_type));
      } catch (std::runtime_error &err) {
        ctx.error(err);
      }
    }
  });
}

template<> void ClassDef<BPF>::init() {
  ctor();
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
