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

#include "codebase-store.hpp"
#include "compress.hpp"
#include "tar.hpp"
#include "utils.hpp"

#include "tutorial.tar.gz.h"

#include <iostream>
#include <sstream>
#include <set>
#include <map>

namespace pipy {

static Data::Producer s_dp("Codebase Store");

//
// Database schema:
//
// files/[id]
//   [file content]
//
// files/tree/[path]
//   id=[file id]
//   version=[version]
//
// codebases/[id]
//   version=[number]
//   path=[codebase path]
//   base=[codebase id]
//   main=[main file path]
//
// codebases/tree/[path]
//   [codebase id]
//
// codebases/[id]/files/[path]
//   [file id]
//
// codebases/[id]/edit/[path]
//   [file id]
//
// codebases/[id]/erased/[path]
//   [file id]
//
// codebases/[id]/derived/[derived codebase id]
//   [version]
//

static std::string KEY_file(const std::string &id) {
  static std::string prefix("files/");
  return prefix + id;
}

static std::string KEY_file_tree(const std::string &path) {
  static std::string prefix("files/tree/");
  return prefix + path;
}

static std::string KEY_codebase(const std::string &id) {
  static std::string prefix("codebases/");
  return prefix + id;
}

static std::string KEY_codebase_tree(const std::string &path) {
  static std::string prefix("codebases/tree/");
  return prefix + path;
}

static std::string KEY_codebase_file(const std::string &id, const std::string &path) {
  static std::string prefix("/files/");
  return KEY_codebase(id) + prefix + path;
}

static std::string KEY_codebase_edit(const std::string &id, const std::string &path) {
  static std::string prefix("/edit/");
  return KEY_codebase(id) + prefix + path;
}

static std::string KEY_codebase_erased(const std::string &id, const std::string &path) {
  static std::string prefix("/erased/");
  return KEY_codebase(id) + prefix + path;
}

static std::string KEY_codebase_derived(const std::string &id, const std::string &derived_id) {
  static std::string prefix("/derived/");
  return KEY_codebase(id) + prefix + derived_id;
}

static auto make_record(const std::map<std::string, std::string> &rec) -> std::string {
  std::string str;
  for (const auto &kv : rec) {
    str += kv.first;
    str += '=';
    str += kv.second;
    str += '\n';
  }
  return str;
}

static void read_record(const std::string &str, std::map<std::string, std::string> &rec) {
  auto lines = utils::split(str, '\n');
  for (const auto &line : lines) {
    auto p = line.find('=');
    if (p != std::string::npos) {
      auto k = line.substr(0 , p);
      auto v = line.substr(p + 1);
      rec[k] = v;
    }
  }
}

//
// CodebaseStore
//

CodebaseStore::CodebaseStore(Store *store)
  : m_store(store)
{
  Data input(s_tutorial_tar_gz, sizeof(s_tutorial_tar_gz), &s_dp), output;
  auto decompressor = Decompressor::inflate(
    [&](Data *data) {
      output.push(*data);
    }
  );
  decompressor->process(&input);
  decompressor->end();

  auto len = output.size();
  char buf[output.size()];
  output.to_bytes((uint8_t *)buf);

  Tarball tarball(buf, len);
  std::set<std::string> filenames;
  tarball.list(filenames);

  std::map<std::string, std::set<std::string>> codebases;
  for (const auto &filename : filenames) {
    auto i = filename.find('/', 1);
    if (i != std::string::npos) {
      auto codebase_name = filename.substr(0, i);
      auto &codebase = codebases[codebase_name];
      codebase.insert(filename.substr(i));
    }
  }

  std::string base_path("/tutorial");
  Codebase *base = nullptr;
  for (const auto &i : codebases) {
    auto path = base_path + i.first;
    auto codebase = find_codebase(path);
    if (!codebase) {
      codebase = make_codebase(path, 0, base);
      if (!base) {
        codebase->erase_file("/main.js");
        codebase->set_main("/hello.js");
      }
      for (const auto &name : i.second) {
        size_t size;
        if (auto data = tarball.get(i.first + name, size)) {
          codebase->set_file(name, Data(data, &s_dp));
          if (name == "/proxy.js") {
            codebase->set_main(name);
          }
        }
      }
      codebase->commit(1);
    }
    base = codebase;
  }
}

CodebaseStore::~CodebaseStore() {
  for (const auto &i : m_codebases) {
    delete i.second;
  }
}

auto CodebaseStore::codebase(const std::string &id) -> Codebase* {
  if (id.empty()) return nullptr;
  Data data;
  auto i = m_codebases.find(id);
  if (i != m_codebases.end()) return i->second;
  if (!m_store->get(KEY_codebase(id), data)) return nullptr;
  auto codebase = new Codebase(this, id);
  m_codebases[id] = codebase;
  return codebase;
}

bool CodebaseStore::find_file(const std::string &path, Data &data, std::string &version) {
  Data buf;
  if (!m_store->get(KEY_file_tree(path), buf)) return false;
  std::map<std::string, std::string> rec;
  read_record(buf.to_string(), rec);
  if (!m_store->get(KEY_file(rec["id"]), data)) return false;
  version = rec["version"];
  return true;
}

auto CodebaseStore::find_codebase(const std::string &path) -> Codebase* {
  Data buf;
  if (!m_store->get(KEY_codebase_tree(path), buf)) return nullptr;
  return codebase(buf.to_string());
}

void CodebaseStore::list_codebases(const std::string &prefix, std::set<std::string> &paths) {
  std::set<std::string> keys;
  auto base_key = KEY_codebase_tree(prefix);
  m_store->keys(base_key, keys);
  base_key = KEY_codebase_tree("");
  for (const auto &k : keys) paths.insert(k.substr(base_key.length()));
}

auto CodebaseStore::make_codebase(const std::string &path, int version, Codebase* base) -> Codebase* {
  std::map<std::string, std::string> rec;
  std::map<std::string, std::string> files;
  std::string codebase_id, main_file_path;

  utils::gen_uuid_v4(codebase_id);

  Store::Batch *batch = nullptr;

  if (base) {
    load_codebase(base->id(), rec);
    auto base_id = base->id();
    list_files(base_id, true, files);
    rec["base"] = base_id;
    main_file_path = rec["main"];
    batch = m_store->batch();
    batch->set(
      KEY_codebase_derived(base_id, codebase_id),
      Data(rec["version"], &s_dp)
    );

  } else {
    std::string main_file_id;
    utils::gen_uuid_v4(main_file_id);
    main_file_path = "/main.js";
    rec["main"] = main_file_path;
    files[main_file_path] = main_file_id;
    batch = m_store->batch();
    batch->set(
      KEY_file(main_file_id),
      Data("pipy()", &s_dp)
    );
    batch->set(
      KEY_codebase_file(codebase_id, main_file_path),
      Data(main_file_id, &s_dp)
    );
  }

  rec["version"] = "1";
  rec["path"] = path;

  batch->set(KEY_codebase(codebase_id), Data(make_record(rec), &s_dp));
  batch->set(KEY_codebase_tree(path), Data(codebase_id, &s_dp));

  generate_files(
    batch,
    path,
    main_file_path,
    std::to_string(version),
    files
  );

  batch->commit();
  return codebase(codebase_id);
}

void CodebaseStore::dump() {
  m_store->dump(std::cout);
}

bool CodebaseStore::load_codebase_if_exists(
  const std::string &id,
  std::map<std::string, std::string> &rec
) {
  Data buf;
  if (!m_store->get(KEY_codebase(id), buf)) return false;
  read_record(buf.to_string(), rec);
  return true;
}

void CodebaseStore::load_codebase(
  const std::string &id,
  std::map<std::string, std::string> &rec
) {
  if (!load_codebase_if_exists(id, rec)) {
    std::string msg("codebase not found: ");
    throw std::runtime_error(msg + id);
  }
}

void CodebaseStore::list_files(
  const std::string &codebase_id,
  bool recursive,
  std::map<std::string, std::string> &files
) {
  auto list_files = [&](const std::string &id) {
    std::set<std::string> keys;
    auto base_key = KEY_codebase_file(id, "");
    m_store->keys(base_key, keys);
    for (const auto &k : keys) {
      auto path = k.substr(base_key.length());
      if (recursive && files.count(path)) continue;
      Data buf;
      if (m_store->get(k, buf)) {
        files[path] = buf.to_string();
      }
    }
  };

  list_files(codebase_id);

  if (recursive) {
    std::map<std::string, std::string> rec;
    load_codebase(codebase_id, rec);

    auto base_id = rec["base"];
    while (!base_id.empty()) {
      list_files(base_id);
      std::map<std::string, std::string> rec;
      load_codebase(base_id, rec);
      base_id = rec["base"];
    }
  }
}

void CodebaseStore::list_derived(const std::string &codebase_id, std::set<std::string> &ids) {
  auto base_key = KEY_codebase_derived(codebase_id, "");
  std::set<std::string> keys;
  m_store->keys(base_key, keys);
  for (const auto &key : keys) {
    auto id = key.substr(base_key.length());
    ids.insert(id);
  }
}

void CodebaseStore::generate_files(
  Store::Batch *batch,
  const std::string &codebase_path,
  const std::string &main_file_path,
  const std::string &version,
  const std::map<std::string, std::string> &files
) {
  std::set<std::string> old_keys;
  m_store->keys(KEY_file_tree(codebase_path), old_keys);

  for (const auto &i : files) {
    auto &path = i.first;
    auto &file_id = i.second;
    auto key = KEY_file_tree(codebase_path + path);
    batch->set(
      key,
      Data(make_record({
        { "id", file_id },
        { "version", version },
      }), &s_dp)
    );
    old_keys.erase(key);
  }

  for (const auto &key : old_keys) {
    batch->erase(key);
  }

  std::string manifest(main_file_path);
  for (const auto &i : files) {
    auto path = i.first;
    if (path == main_file_path) continue;
    manifest += '\n';
    manifest += path;
  }

  Data buf;
  auto key = KEY_file_tree(codebase_path) + '/';
  std::string manifest_id;

  if (m_store->get(key, buf)) {
    std::map<std::string, std::string> rec;
    read_record(buf.to_string(), rec);
    manifest_id = rec["id"];
  }

  if (manifest_id.empty()) {
    utils::gen_uuid_v4(manifest_id);
  }

  batch->set(
    key,
    Data(make_record({
      { "id", manifest_id },
      { "version", version },
    }), &s_dp)
  );

  batch->set(
    KEY_file(manifest_id),
    Data(manifest, &s_dp)
  );
}

//
// CodebaseStore::Codebase
//

void CodebaseStore::Codebase::get_info(Info &info) {
  std::map<std::string, std::string> rec;
  m_code_store->load_codebase(m_id, rec);
  info.version = std::atoi(rec["version"].c_str());
  info.path = rec["path"];
  info.base = rec["base"];
  info.main = rec["main"];
}

bool CodebaseStore::Codebase::get_file(const std::string &path, std::string &id) {
  Data buf;
  auto store = m_code_store->m_store;
  id.clear();

  if (!store->get(KEY_codebase_erased(m_id, path), buf)) {
    if (id.empty()) if (store->get(KEY_codebase_edit(m_id, path), buf)) id = buf.to_string();
    if (id.empty()) if (store->get(KEY_codebase_file(m_id, path), buf)) id = buf.to_string();
  }

  auto base_id = m_id;
  while (id.empty()) {
    std::map<std::string, std::string> info;
    m_code_store->load_codebase(base_id, info);
    base_id = info["base"];
    if (base_id.empty()) break;
    if (store->get(KEY_codebase_file(base_id, path), buf)) id = buf.to_string();
  }

  return !id.empty();
}

bool CodebaseStore::Codebase::get_file(const std::string &path, Data &data) {
  std::string id;
  if (!get_file(path, id)) return false;
  return m_code_store->m_store->get(KEY_file(id), data);
}

void CodebaseStore::Codebase::set_file(const std::string &path, const Data &data) {
  Data buf;
  auto key = KEY_codebase_edit(m_id, path);
  auto store = m_code_store->m_store;
  auto batch = store->batch();
  if (!store->get(key, buf)) {
    std::string file_id;
    utils::gen_uuid_v4(file_id);
    batch->set(KEY_file(file_id), data);
    batch->set(key, Data(file_id, &s_dp));
  } else {
    batch->set(KEY_file(buf.to_string()), data);
  }
  batch->erase(KEY_codebase_erased(m_id, path));
  batch->commit();
}

void CodebaseStore::Codebase::set_main(const std::string &path) {
  std::map<std::string, std::string> rec;
  m_code_store->load_codebase(m_id, rec);
  rec["main"] = path;
  m_code_store->m_store->set(KEY_codebase(m_id), Data(make_record(rec), &s_dp));
}

void CodebaseStore::Codebase::list_derived(std::set<std::string> &paths) {
  std::set<std::string> ids;
  m_code_store->list_derived(m_id, ids);
  for (const auto &id : ids) {
    std::map<std::string, std::string> rec;
    m_code_store->load_codebase(id, rec);
    auto path = rec["path"];
    if (!path.empty()) paths.insert(path);
  }
}

void CodebaseStore::Codebase::list_files(bool recursive, std::set<std::string> &paths) {
  auto store = m_code_store->m_store;
  auto base_key = KEY_codebase_file(m_id, "");
  std::set<std::string> keys;
  store->keys(base_key, keys);
  for (const auto &key : keys) paths.insert(key.substr(base_key.length()));
  if (recursive) {
    std::map<std::string, std::string> rec;
    m_code_store->load_codebase(m_id, rec);
    auto base_id = rec["base"];
    if (!base_id.empty()) {
      if (auto base = m_code_store->codebase(base_id)) {
        base->list_files(true, paths);
      }
    }
  }
}

void CodebaseStore::Codebase::list_edit(std::set<std::string> &paths) {
  auto store = m_code_store->m_store;
  auto base_key = KEY_codebase_edit(m_id, "");
  std::set<std::string> keys;
  store->keys(base_key, keys);
  for (const auto &key : keys) paths.insert(key.substr(base_key.length()));
}

void CodebaseStore::Codebase::list_erased(std::set<std::string> &paths) {
  auto store = m_code_store->m_store;
  auto base_key = KEY_codebase_erased(m_id, "");
  std::set<std::string> keys;
  store->keys(base_key, keys);
  for (const auto &key : keys) paths.insert(key.substr(base_key.length()));
}

void CodebaseStore::Codebase::erase_file(const std::string &path) {
  Data buf;
  auto store = m_code_store->m_store;
  auto key = KEY_codebase_edit(m_id, path);
  auto *batch = store->batch();
  if (store->get(key, buf)) {
    auto file_id = buf.to_string();
    batch->erase(KEY_file(file_id));
  }
  batch->erase(key);
  key = KEY_codebase_file(m_id, path);
  if (store->get(key, buf)) {
    batch->set(KEY_codebase_erased(m_id, path), buf);
  }
  batch->commit();
}

void CodebaseStore::Codebase::reset_file(const std::string &path) {
  Data buf;
  auto store = m_code_store->m_store;
  auto key = KEY_codebase_edit(m_id, path);
  auto *batch = store->batch();
  if (store->get(key, buf)) {
    auto file_id = buf.to_string();
    batch->erase(KEY_file(file_id));
  }
  batch->erase(key);
  batch->erase(KEY_codebase_erased(m_id, path));
  batch->commit();
}

void CodebaseStore::Codebase::commit(int version) {
  Data buf;

  std::map<std::string, std::string> info;
  std::map<std::string, std::string> files;
  m_code_store->load_codebase(m_id, info);
  m_code_store->list_files(m_id, false, files);

  std::set<std::string> edit, erased;
  list_edit(edit);
  list_erased(erased);

  auto version_str = std::to_string(version);
  info["version"] = version_str;

  auto store = m_code_store->m_store;
  auto batch = store->batch();

  for (const auto &path : edit) {
    auto key = KEY_codebase_edit(m_id, path);
    if (store->get(key, buf)) {
      auto id = buf.to_string();
      auto i = files.find(path);
      if (i == files.end()) {
        files[path] = id;
      } else {
        batch->erase(KEY_file(i->second));
        i->second = id;
      }
      batch->set(KEY_codebase_file(m_id, path), buf);
      batch->erase(key);
    }
  }

  for (const auto &path : erased) {
    auto key = KEY_codebase_erased(m_id, path);
    if (store->get(key, buf)) {
      auto id = buf.to_string();
      batch->erase(KEY_file(id));
      batch->erase(KEY_codebase_file(m_id, path));
      batch->erase(key);
      files.erase(path);
    }
  }

  auto base_id = info["base"];
  if (!base_id.empty()) {
    std::map<std::string, std::string> base;
    m_code_store->list_files(base_id, true, base);
    for (const auto &i : base) {
      if (!files.count(i.first)) {
        files[i.first] = i.second;
      }
    }
  }

  m_code_store->generate_files(
    batch,
    info["path"],
    info["main"],
    version_str,
    files
  );

  batch->set(
    KEY_codebase(m_id),
    Data(make_record(info), &s_dp)
  );

  std::function<
    void(
      const std::string &id,
      const std::string &version,
      const std::map<std::string, std::string> &base
    )
  > upgrade_derived;

  bool has_any_upgrades = false;

  upgrade_derived = [&](
    const std::string &id,
    const std::string &version,
    const std::map<std::string, std::string> &base
  ) {
    std::set<std::string> derived;
    m_code_store->list_derived(id, derived);
    for (const auto &derived_id : derived) {
      std::map<std::string, std::string> info;
      std::map<std::string, std::string> files(base);
      m_code_store->load_codebase(derived_id, info);
      m_code_store->list_files(derived_id, false, files);
      std::string derived_version = info["version"];
      std::string derived_key = KEY_codebase_derived(id, derived_id);
      Data buf;
      store->get(derived_key, buf);
      if (buf.to_string() != version) {
        derived_version = std::to_string(std::atoi(derived_version.c_str()) + 1);
        info["version"] = derived_version;
        m_code_store->generate_files(batch, info["path"], info["main"], derived_version, files);
        batch->set(KEY_codebase(derived_id), Data(make_record(info), &s_dp));
        batch->set(derived_key, Data(version, &s_dp));
        has_any_upgrades = true;
      }
      upgrade_derived(derived_id, derived_version, files);
    }
  };

  upgrade_derived(m_id, version_str, files);

  return batch->commit();
}

void CodebaseStore::Codebase::reset() {
  std::set<std::string> keys;
  auto base_key = KEY_codebase_edit(m_id, "");
  auto store = m_code_store->m_store;
  store->keys(base_key, keys);
  auto batch = store->batch();
  for (const auto &key : keys) {
    Data buf;
    if (!store->get(key, buf)) continue;
    batch->erase(KEY_file(buf.to_string()));
    batch->erase(key);
  }
  return batch->commit();
}

} // namespace pipy