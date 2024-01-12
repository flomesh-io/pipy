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

#include "elf.hpp"
#include "log.hpp"

#include <stdexcept>

#ifdef PIPY_USE_BPF
#include <elf.h>
#include <linux/btf.h>
#endif // PIPY_USE_BPF

namespace pipy {

#ifdef PIPY_USE_BPF

static void section_out_of_bound(size_t i) {
  std::string msg("out of bound section: index = ");
  msg += std::to_string(i);
  throw std::runtime_error(msg);
}

static void relocation_out_of_bound(size_t i) {
  std::string msg("out of bound relocation: index = ");
  msg += std::to_string(i);
  throw std::runtime_error(msg);
}

ELF::ELF(std::vector<uint8_t> &&data) : m_data(std::move(data)) {
  if (m_data.size() <= EI_NIDENT ||
    m_data[EI_MAG0] != ELFMAG0 ||
    m_data[EI_MAG1] != ELFMAG1 ||
    m_data[EI_MAG2] != ELFMAG2 ||
    m_data[EI_MAG3] != ELFMAG3 ||
    m_data[EI_CLASS] <= ELFCLASSNONE ||
    m_data[EI_CLASS] >= ELFCLASSNUM ||
    m_data[EI_VERSION] != EV_CURRENT
  ) throw std::runtime_error("not an ELF file");

  if (
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    m_data[EI_DATA] != ELFDATA2LSB
#else
    m_data[EI_DATA] != ELFDATA2MSB
#endif
  ) throw std::runtime_error("mismatched ELF endianness");

  if (m_data[EI_OSABI] != ELFOSABI_SYSV) {
    throw std::runtime_error("unsupported ABI");
  }

  auto cls = m_data[EI_CLASS];

  size_t phoff, shoff, ehsize, shstrndx;
  size_t phentsize, phnum;
  size_t shentsize, shnum;

  (void)ehsize;
  (void)phoff;
  (void)phentsize;
  (void)phnum;

  switch (cls) {
    case ELFCLASS32: {
      auto &hdr = *(Elf32_Ehdr *)m_data.data();
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
      auto &hdr = *(Elf64_Ehdr *)m_data.data();
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

  if (shoff + shentsize * shnum > m_data.size() || shstrndx >= shnum) {
    std::runtime_error("offset out of ELF file boundary");
  }

  m_str_tab_idx = shstrndx;

  std::vector<size_t> name_offsets(shnum);
  sections.resize(shnum);

  for (size_t i = 0; i < shnum; i++) {
    auto offset = shoff + shentsize * i;
    auto &sec = sections[i];
    switch (cls) {
      case ELFCLASS32: {
        auto &hdr = *(Elf32_Shdr *)(m_data.data() + offset);
        if (hdr.sh_offset + hdr.sh_size > m_data.size()) {
          section_out_of_bound(i);
        }
        name_offsets[i] = hdr.sh_name;
        sec.type = hdr.sh_type;
        sec.flags = hdr.sh_flags;
        sec.addr = hdr.sh_addr;
        sec.data = m_data.data() + hdr.sh_offset;
        sec.size = hdr.sh_size;
        sec.link = hdr.sh_link;
        sec.info = hdr.sh_info;
        sec.addralign = hdr.sh_addralign;
        break;
      }
      case ELFCLASS64: {
        auto &hdr = *(Elf64_Shdr *)(m_data.data() + offset);
        if (hdr.sh_offset + hdr.sh_size > m_data.size()) {
          section_out_of_bound(i);
        }
        name_offsets[i] = hdr.sh_name;
        sec.type = hdr.sh_type;
        sec.flags = hdr.sh_flags;
        sec.addr = hdr.sh_addr;
        sec.data = m_data.data() + hdr.sh_offset;
        sec.size = hdr.sh_size;
        sec.link = hdr.sh_link;
        sec.info = hdr.sh_info;
        sec.addralign = hdr.sh_addralign;
        break;
      }
    }
  }

  for (size_t i = 0; i < shnum; i++) {
    auto &s = sections[i];
    s.name = string(name_offsets[i]);
    Log::debug(Log::ELF,
      "[elf] SECTION #%d name '%s' addr 0x%08x size %d type %d flags %d link %d info %d",
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
            s.name = string(ent.st_name);
            s.value = ent.st_value;
            s.size = ent.st_size;
            s.shndx = ent.st_shndx;
            s.type = ELF32_ST_TYPE(ent.st_info);
            s.bind = ELF32_ST_BIND(ent.st_info);
            s.visibility = ELF32_ST_VISIBILITY(ent.st_other);
            break;
          }
          case ELFCLASS64: {
            const auto &ent = *(Elf64_Sym *)(sec.data + offset);
            s.name = string(ent.st_name);
            s.value = ent.st_value;
            s.size = ent.st_size;
            s.shndx = ent.st_shndx;
            s.type = ELF64_ST_TYPE(ent.st_info);
            s.bind = ELF64_ST_BIND(ent.st_info);
            s.visibility = ELF64_ST_VISIBILITY(ent.st_other);
            break;
          }
        }
        Log::debug(Log::ELF,
          "[elf] SYMBOL #%d name '%s' value %d size %d shndx %d type %d bind %d visibility %d",
          int(i), s.name.c_str(), int(s.value), int(s.size), int(s.shndx), s.type, s.bind, s.visibility
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
            r.sym = ELF32_R_SYM(ent.r_info);
            r.type = ELF32_R_TYPE(ent.r_info);
            break;
          }
          case ELFCLASS64: {
            const auto &ent = *(Elf64_Rel *)(sec.data + offset);
            r.offset = ent.r_offset;
            r.sym = ELF64_R_SYM(ent.r_info);
            r.type = ELF64_R_TYPE(ent.r_info);
            break;
          }
        }
        Log::debug(Log::ELF,
          "[elf] RELOC for SECTION #%d offset %d sym %d type %d",
          int(reloc.section), int(r.offset), r.sym, r.type
        );
        if (r.offset >= max_offset) {
          relocation_out_of_bound(i);
        }
      }
    }
  }
}

auto ELF::string(size_t offset) const -> std::string {
  const auto &str_tab = sections[m_str_tab_idx];
  auto data = str_tab.data;
  auto size = str_tab.size;
  if (offset >= size) {
    throw std::runtime_error(
      "string offset out of bound: offset = " + std::to_string(offset)
    );
  }
  auto end = offset;
  while (end < size && data[end]) end++;
  return std::string((const char *)(data + offset), end - offset);
}

//
// BTF
//

BTF::BTF(const ELF &elf, size_t sec) {
  auto &s = elf.sections[sec];
  auto ptr = s.data;
  auto end = s.data + s.size;
  auto idx = 1;
  auto hdr = (struct btf_header *)ptr;

  if (hdr->magic != 0xeb9f) {
    throw std::runtime_error(hdr->magic == 0x9feb
      ? "incorrect endianness of BTF data"
      : "incorrect BTF magic number"
    );
  }

  ptr += hdr->hdr_len;
  if (ptr > end) {
    throw std::runtime_error("BTF header out of bound");
  }

  auto str_ptr = ptr + hdr->str_off;
  auto str_end = ptr + hdr->str_off + hdr->str_len;
  if (str_end > end) throw std::runtime_error("BTF string section out of bound");

  ptr += hdr->type_off;
  if (ptr + hdr->type_len > end) {
    throw std::runtime_error("BTF type section out of bound");
  }
  end = ptr + hdr->type_len;

  auto string = [&](size_t offset) {
    auto s = str_ptr + offset;
    if (s > str_end) {
      throw std::runtime_error(
        "BTF string offset out of bound: offset = " + std::to_string(offset)
      );
    }
    auto end = s;
    while (end < str_end && *end) end++;
    return std::string((const char *)s, end - s);
  };

  types.push_back(nullptr); // index 0 for void type

  while (ptr + sizeof(struct btf_type) <= end) {
    auto &bt = *(struct btf_type *)ptr;
    auto name = string(bt.name_off);
    auto vlen = BTF_INFO_VLEN(bt.info);
    auto kind = BTF_INFO_KIND(bt.info);
    auto kind_flag = BTF_INFO_KFLAG(bt.info);
    ptr += sizeof(struct btf_type);
    Type *type = nullptr;
    switch (kind) {
      case BTF_KIND_INT: {
        if (ptr + sizeof(__u32) > end) break;
        auto info = *(__u32 *)ptr;
        auto i = new Int;
        auto encoding = BTF_INT_ENCODING(info);
        i->offset = BTF_INT_OFFSET(info);
        i->bits = BTF_INT_BITS(info);
        i->is_signed = encoding & BTF_INT_SIGNED;
        i->is_char = encoding & BTF_INT_CHAR;
        i->is_bool = encoding & BTF_INT_BOOL;
        ptr += sizeof(__u32);
        type = i;
        Log::debug(
          Log::ELF, "[elf] BTF #%d '%s' int offset %d bits %d signed %d char %d bool %d",
          idx, name.c_str(), (int)i->offset, (int)i->bits, i->is_signed, i->is_char, i->is_bool
        );
        break;
      }
      case BTF_KIND_ARRAY: {
        if (ptr + sizeof(struct btf_array) > end) break;
        auto &ba = *(struct btf_array *)ptr;
        auto a = new Array;
        a->elem_type = ba.type;
        a->index_type = ba.index_type;
        a->nelems = ba.nelems;
        ptr += sizeof(ba);
        type = a;
        Log::debug(
          Log::ELF, "[elf] BTF #%d '%s' array type %d index_type %d nelems %d",
          idx, name.c_str(), a->type, a->index_type, a->nelems
        );
        break;
      }
      case BTF_KIND_STRUCT:
      case BTF_KIND_UNION: {
        if (ptr + sizeof(struct btf_member) * vlen > end) break;
        auto s = new Struct;
        for (size_t i = 0; i < vlen; i++) {
          auto &bm = *(struct btf_member *)ptr;
          Member m;
          m.name = string(bm.name_off);
          m.type = bm.type;
          m.offset = bm.offset;
          s->members.push_back(m);
          ptr += sizeof(bm);
        }
        type = s;
        if (Log::is_enabled(Log::ELF)) {
          Log::debug(Log::ELF, "[elf] BTF #%d '%s' %s", idx, name.c_str(), kind == BTF_KIND_UNION ? "union" : "struct");
          for (const auto &m : s->members) {
            Log::debug(Log::ELF, "[elf]   '%s' type %d offset %d", m.name.c_str(), m.type, m.offset);
          }
        }
        break;
      }
      case BTF_KIND_ENUM: {
        if (ptr + sizeof(struct btf_enum) * vlen > end) break;
        auto e = new Enum;
        for (size_t i = 0; i < vlen; i++) {
          auto &be = *(struct btf_enum *)ptr;
          e->values[string(be.name_off)] = be.val;
          ptr += sizeof(be);
        }
        type = e;
        if (Log::is_enabled(Log::ELF)) {
          Log::debug(Log::ELF, "[elf] BTF #%d '%s' enum", idx, name.c_str());
          for (const auto &p : e->values) {
            Log::debug(Log::ELF, "[elf]   '%s' = %d", p.first.c_str(), p.second);
          }
        }
        break;
      }
      case BTF_KIND_FUNC_PROTO: {
        if (ptr + sizeof(struct btf_param) * vlen > end) break;
        auto fp = new FuncProto;
        for (size_t i = 0; i < vlen; i++) {
          auto &bp = *(struct btf_param *)ptr;
          Param p;
          p.name = string(bp.name_off);
          p.type = bp.type;
          fp->params.push_back(p);
          ptr += sizeof(bp);
        }
        type = fp;
        if (Log::is_enabled(Log::ELF)) {
          Log::debug(Log::ELF, "[elf] BTF #%d '%s' func proto type %d", idx, name.c_str(), (int)bt.type);
          for (const auto &p : fp->params) {
            Log::debug(Log::ELF, "[elf]   '%s' type %d", p.name.c_str(), p.type);
          }
        }
        break;
      }
      case BTF_KIND_VAR: {
        if (ptr + sizeof(struct btf_var) > end) break;
        auto v = new Var;
        auto &bv = *(struct btf_var *)ptr;
        v->linkage = bv.linkage;
        ptr += sizeof(bv);
        type = v;
        Log::debug(Log::ELF, "[elf] BTF #%d '%s' var type %d linkage %d", idx, name.c_str(), (int)bt.type, v->linkage);
        break;
      }
      case BTF_KIND_DATASEC: {
        if (ptr + sizeof(struct btf_var_secinfo) > end) break;
        auto ds = new DataSec;
        for (size_t i = 0; i < vlen; i++) {
          auto &bvs = *(struct btf_var_secinfo *)ptr;
          VarSecInfo vsi;
          vsi.type = bvs.type;
          vsi.offset = bvs.offset;
          vsi.size = bvs.size;
          ds->vars.push_back(vsi);
          ptr += sizeof(bvs);
        }
        type = ds;
        if (Log::is_enabled(Log::ELF)) {
          Log::debug(Log::ELF, "[elf] BTF #%d '%s' datasec", idx, name.c_str());
          for (const auto &v : ds->vars) {
            Log::debug(Log::ELF, "[elf]   type %d offset %d size %d", (int)v.type, (int)v.offset, (int)v.size);
          }
        }
        break;
      }
      case BTF_KIND_FWD: {
        type = new Type;
        Log::debug(Log::ELF, "[elf] BTF #%d '%s' fwd", idx, name.c_str());
        break;
      }
      case BTF_KIND_PTR:
      case BTF_KIND_TYPEDEF:
      case BTF_KIND_VOLATILE:
      case BTF_KIND_CONST:
      case BTF_KIND_RESTRICT:
      case BTF_KIND_FUNC:
        type = new Type;
        if (Log::is_enabled(Log::ELF)) {
          const char *k = nullptr;
          switch (kind) {
            case BTF_KIND_PTR: k = "ptr"; break;
            case BTF_KIND_FWD: k = "fwd"; break;
            case BTF_KIND_TYPEDEF: k = "typedef"; break;
            case BTF_KIND_VOLATILE: k = "volatile"; break;
            case BTF_KIND_CONST: k = "const"; break;
            case BTF_KIND_RESTRICT: k = "restrict"; break;
            case BTF_KIND_FUNC: k = "func"; break;
            default: k = ""; break;
          }
          Log::debug(Log::ELF, "[elf] BTF #%d '%s' %s type %d", idx, name.c_str(), k, bt.type);
        }
        break;
      default: {
        std::string msg("unknown BTF kind ");
        msg += std::to_string(kind);
        throw std::runtime_error(msg);
      }
    }
    type->name = name;
    type->kind = kind;
    type->kind_flag = kind_flag;
    type->type = bt.type;
    types.push_back(std::unique_ptr<Type>(type));
    idx++;
  }
}

#else // !PIPY_USE_BPF

static void unsupported() {
  throw std::runtime_error("eBPF not supported");
}

ELF::ELF(std::vector<uint8_t> &&data) {
  unsupported();
}

BTF::BTF(const ELF &elf, size_t sec) {
  unsupported();  
}

#endif // PIPY_USE_BPF

} // namespace pipy
