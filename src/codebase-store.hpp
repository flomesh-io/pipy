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

#ifndef CODEBASE_STORE_HPP
#define CODEBASE_STORE_HPP

#include "store.hpp"
#include "pjs/pjs.hpp"

#include <map>
#include <set>

namespace pipy {

class CodebaseStore {
public:
  CodebaseStore(Store *store);
  ~CodebaseStore();

  class Codebase {
  public:
    struct Info {
      int version;
      std::string path;
      std::string base;
      std::string main;
    };

    Codebase(CodebaseStore *store, const std::string &id)
      : m_code_store(store)
      , m_id(id) {}

    auto id() const -> const std::string& { return m_id; }

    void get_info(Info &info);
    bool get_file(const std::string &path, std::string &id);
    bool get_file(const std::string &path, Data &data);
    void set_file(const std::string &path, const Data &data);
    void set_main(const std::string &path);
    void list_derived(std::set<std::string> &names);
    void list_files(bool recursive, std::set<std::string> &paths);
    void list_edit(std::set<std::string> &paths);
    void list_erased(std::set<std::string> &paths);
    void erase_file(const std::string &path);
    void reset_file(const std::string &path);
    bool commit(int version, std::list<std::string> &updated);
    void erase();
    void reset();

  private:
    CodebaseStore* m_code_store;
    std::string m_id;

    void erase(Store::Batch *batch, const std::string &codebase_id);

    friend class CodebaseStore;
  };

  auto codebase(const std::string &id) -> Codebase*;

  bool find_file(const std::string &path, Data &data, std::string &version);
  auto find_codebase(const std::string &path) -> Codebase*;
  void list_codebases(const std::string &prefix, std::set<std::string> &paths);
  auto make_codebase(const std::string &path, int version, Codebase* base = nullptr) -> Codebase*;

  void dump();

private:
  Store* m_store;
  std::map<std::string, Codebase*> m_codebases;

  bool load_codebase_if_exists(
    const std::string &id,
    std::map<std::string, std::string> &rec
  );

  void load_codebase(
    const std::string &id,
    std::map<std::string, std::string> &rec
  );

  void list_files(
    const std::string &codebase_id,
    bool recursive,
    std::map<std::string, std::string> &files
  );

  void list_derived(
    const std::string &codebase_id,
    std::set<std::string> &ids
  );

  void generate_files(
    Store::Batch *batch,
    const std::string &codebase_path,
    const std::string &main_file_path,
    const std::string &version,
    const std::map<std::string, std::string> &files
  );

  void erase_codebase(Store::Batch *batch, const std::string &codebase_id);

  friend class Codebase;
};

} // namespace pipy

#endif // CODEBASE_STORE_HPP