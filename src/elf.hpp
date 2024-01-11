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

#ifndef ELF_HPP
#define ELF_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace pipy {

//
// ELF
//

class ELF {
public:

  //
  // ELF::Section
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
  // ELF::Symbol
  //

  struct Symbol {
    std::string name;
    size_t value;
    size_t size;
    size_t shndx;
    int type;
    int bind;
    int visibility;
  };

  //
  // ELF::Relocation
  //

  struct Relocation {

    //
    // ELF::Relocation::Entry
    //

    struct Entry {
      size_t offset;
      int sym;
      int type;
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

  ELF(std::vector<uint8_t> &&data);

  auto string(size_t offset) const -> std::string;

private:
  std::vector<uint8_t> m_data;
  size_t m_str_tab_idx;
};

//
// BTF
//

class BTF {
public:

  //
  // BTF::Type
  //

  struct Type {
    std::string name;
    int kind;
    int kind_flag;
    union {
      size_t size;
      size_t type;
    };

    virtual ~Type() {}
  };

  //
  // BTF::Member
  //

  struct Member {
    std::string name;
    size_t type;
    size_t offset;
  };

  //
  // BTF::Param
  //

  struct Param {
    std::string name;
    size_t type;
  };

  //
  // BTF::VarSecInfo
  //

  struct VarSecInfo {
    size_t type;
    size_t offset;
    size_t size;
  };

  //
  // BTF::Array
  //

  struct Array : public Type {
    size_t type;
    size_t index_type;
    size_t nelems;
  };

  //
  // BTF::Struct
  //

  struct Struct : public Type {
    std::vector<Member> members;
  };

  //
  // BTF::Enum
  //

  struct Enum : public Type {
    std::map<std::string, int> values;
  };

  //
  // BTF::FuncProto
  //

  struct FuncProto : public Type {
    std::vector<Param> params;
  };

  //
  // BTF::Var
  //

  struct Var : public Type {
    int linkage;
  };

  //
  // BTF::DataSec
  //

  struct DataSec : public Type {
    std::vector<VarSecInfo> vars;
  };

  std::vector<std::unique_ptr<Type>> types;

  BTF(const ELF &elf, size_t sec);
};

} // namespace pipy

#endif // ELF_HPP
