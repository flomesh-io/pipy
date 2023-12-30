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
            s.name = string(ent.st_name);
            s.value = ent.st_value;
            s.size = ent.st_size;
            s.shndx = ent.st_shndx;
            s.info = ent.st_info;
            break;
          }
          case ELFCLASS64: {
            const auto &ent = *(Elf64_Sym *)(sec.data + offset);
            s.name = string(ent.st_name);
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

auto ELF::string(size_t offset) const -> std::string {
  const auto &str_tab = sections[m_str_tab_idx];
  auto data = str_tab.data;
  auto size = str_tab.size;
  if (offset >= size) {
    std::string msg("string offset out of bound: offset = ");
    msg += std::to_string(offset);
    throw std::runtime_error(msg);
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
  while (ptr + sizeof(struct btf_type) <= end) {
    auto &bt = *(struct btf_type *)ptr;
    auto name = elf.string(bt.name_off);
    auto vlen = BTF_INFO_VLEN(bt.info);
    auto kind = BTF_INFO_KIND(bt.info);
    auto kind_flag = BTF_INFO_KFLAG(bt.info);
    ptr += sizeof(struct btf_type);
    Type *type = nullptr;
    switch (kind) {
      case BTF_KIND_ARRAY: {
        if (ptr + sizeof(struct btf_array) > end) break;
        auto &ba = *(struct btf_array *)ptr;
        auto a = new Array;
        a->type = ba.type;
        a->index_type = ba.index_type;
        a->nelems = ba.nelems;
        ptr += sizeof(ba);
        type = a;
        break;
      }
      case BTF_KIND_STRUCT:
      case BTF_KIND_UNION: {
        if (ptr + sizeof(struct btf_member) * vlen > end) break;
        auto s = new Struct;
        for (size_t i = 0; i < vlen; i++) {
          auto &bm = *(struct btf_member *)ptr;
          Member m;
          m.name = elf.string(bm.name_off);
          m.type = bm.type;
          m.offset = bm.offset;
          s->members.push_back(m);
          ptr += sizeof(bm);
        }
        type = s;
        break;
      }
      case BTF_KIND_ENUM: {
        if (ptr + sizeof(struct btf_enum) * vlen > end) break;
        auto e = new Enum;
        for (size_t i = 0; i < vlen; i++) {
          auto &be = *(struct btf_enum *)ptr;
          e->values[elf.string(be.name_off)] = be.val;
          ptr += sizeof(be);
        }
        type = e;
        break;
      }
      case BTF_KIND_FUNC_PROTO: {
        if (ptr + sizeof(struct btf_param) * vlen > end) break;
        auto fp = new FuncProto;
        for (size_t i = 0; i < vlen; i++) {
          auto &bp = *(struct btf_param *)ptr;
          Param p;
          p.name = elf.string(bp.name_off);
          p.type = bp.type;
          fp->params.push_back(p);
          ptr += sizeof(bp);
        }
        type = fp;
        break;
      }
      case BTF_KIND_VAR: {
        if (ptr + sizeof(struct btf_var) > end) break;
        auto v = new Var;
        auto &bv = *(struct btf_var *)ptr;
        v->linkage = bv.linkage;
        ptr += sizeof(bv);
        type = v;
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
        break;
      }
      case BTF_KIND_INT:
      case BTF_KIND_PTR:
      case BTF_KIND_FWD:
      case BTF_KIND_TYPEDEF:
      case BTF_KIND_VOLATILE:
      case BTF_KIND_CONST:
      case BTF_KIND_RESTRICT:
      case BTF_KIND_FUNC:
        type = new Type;
        break;
      default: {
        std::string msg("unknown BTF kind ");
        msg += std::to_string(kind);
        throw std::runtime_error(msg);
      }
    }
    type->kind = kind;
    type->kind_flag = kind_flag;
    type->size = bt.size;
    types.push_back(std::unique_ptr<Type>(type));
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
