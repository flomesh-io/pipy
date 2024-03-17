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

#ifndef API_BPF_HPP
#define API_BPF_HPP

#include "pjs/pjs.hpp"
#include "api/c-struct.hpp"
#include "elf.hpp"
#include "data.hpp"

namespace pipy {
namespace bpf {

class Program;
class Map;

//
// ObjectFile
//

class ObjectFile : public pjs::ObjectTemplate<ObjectFile> {
public:
  std::vector<pjs::Ref<Program>> programs;
  std::vector<pjs::Ref<Map>> maps;

private:
  ObjectFile(Data *data);

  void find_maps(const BTF &btf);
  auto find_type(const BTF &btf, size_t type) -> const BTF::Type*;
  auto make_struct(const BTF &btf, size_t type) -> CStructBase*;

  friend class pjs::ObjectTemplate<ObjectFile>;
};

//
// Program
//

class Program : public pjs::ObjectTemplate<Program> {
public:
  static auto list() -> pjs::Array*;

  struct Reloc {
    int position;
    pjs::Ref<Map> map;
  };

  auto name() const -> pjs::Str* { return m_name; }
  auto size() const -> int;
  auto fd() const -> int { return m_fd; }
  auto id() const -> int { return m_id; }

  void load(int type, int attach_type, const std::string &license);

private:
  Program(
    const std::string &name,
    std::vector<uint8_t> &insts,
    std::vector<Reloc> &relocs
  );

  pjs::Ref<pjs::Str> m_name;
  std::vector<uint8_t> m_insts;
  std::vector<Reloc> m_relocs;

  int m_fd = 0;
  int m_id = 0;

  friend class pjs::ObjectTemplate<Program>;
};

//
// Map
//

class Map : public pjs::ObjectTemplate<Map> {
public:

  //
  // Map::Info
  //

  struct Info : public pjs::ObjectTemplate<Info> {
    pjs::Ref<pjs::Str> name;
    int id = 0;
    int type = 0;
    int flags = 0;
    int maxEntries = 0;
    int keySize = 0;
    int valueSize = 0;
  };

  static auto list() -> pjs::Array*;
  static auto open(int id, CStructBase *key_type = nullptr, CStructBase *value_type = nullptr) -> Map*;

  auto name() const -> pjs::Str* { return m_name; }
  auto fd() const -> int { return m_fd; }
  auto id() const -> int { return m_id; }
  auto type() const -> int { return m_type; }
  auto flags() const -> int { return m_flags; }
  auto max_entries() const -> int { return m_max_entries; }
  auto key_size() const -> int { return m_key_size; }
  auto key_type() const -> CStructBase* { return m_key_type; }
  auto value_size() const -> int { return m_value_size; }
  auto value_type() const -> CStructBase* { return m_value_type; }

  void create();
  auto keys() -> pjs::Array*;
  auto entries() -> pjs::Array*;
  auto lookup(pjs::Object *key) -> pjs::Object*;
  void update(pjs::Object *key, pjs::Object *value);
  void remove(pjs::Object *key);
  void close();

private:
  Map(const std::string &name, int type, int flags, int max_entries, int key_size, int value_size);
  Map(const std::string &name, int type, int flags, int max_entries, CStructBase *key_type = nullptr, CStructBase *value_type = nullptr);
  Map(int fd, CStructBase *key_type = nullptr, CStructBase *value_type = nullptr);

  auto lookup_raw(Data *key) -> Data*;
  void update_raw(Data *key, Data *value);
  void delete_raw(Data *key);

  pjs::Ref<pjs::Str> m_name;
  int m_fd;
  int m_id;
  int m_type;
  int m_flags;
  int m_max_entries;
  int m_key_size;
  int m_value_size;
  pjs::Ref<CStructBase> m_key_type;
  pjs::Ref<CStructBase> m_value_type;

  friend class pjs::ObjectTemplate<Map>;
};

//
// BPF
//

class BPF : public pjs::ObjectTemplate<BPF> {
public:
  static void pin(const std::string &pathname, int fd);
  static void attach(int attach_type, int fd);
  static void detach(int attach_type, int fd);
  static void attach(int attach_type, int fd, const std::string &cgroup);
  static void detach(int attach_type, int fd, const std::string &cgroup);
  static void attach(int attach_type, int fd, int map_fd);
  static void detach(int attach_type, int fd, int map_fd);
  static auto cgroup(const std::string &pathname) -> uint64_t;
};

} // namespace bpf
} // namespace pipy

#endif // API_BPF_HPP
