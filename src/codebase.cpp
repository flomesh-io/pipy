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

#include "codebase.hpp"
#include "compressor.hpp"
#include "data.hpp"
#include "fs.hpp"
#include "timer.hpp"
#include "utils.hpp"

#include <limits.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>

#include "codebases.br.h"

namespace pipy {

static Data::Producer s_dp("Codebase");

thread_local Codebase* Codebase::s_current = nullptr;

//
// CodebaseFromRoot
//

class CodebaseFromRoot : public Codebase {
public:
  CodebaseFromRoot(Codebase *root);
  ~CodebaseFromRoot();

  virtual bool writable() const override { return m_root->writable(); }
  virtual auto entry() const -> const std::string& override { return m_root->entry(); }
  virtual void entry(const std::string &path) override { m_root->entry(path); }
  virtual void mount(const std::string &path, Codebase *codebase) override;
  virtual auto list(const std::string &path) -> std::list<std::string> override;
  virtual auto get(const std::string &path) -> SharedData* override;
  virtual void set(const std::string &path, SharedData *data) override;
  virtual auto watch(const std::string &path, const std::function<void(const std::list<std::string> &)> &on_update) -> Watch* override;

private:
  Codebase* m_root;
  std::map<std::string, Codebase*> m_mounts;
  std::mutex m_mutex;

  auto find_mount(const std::string &path, std::string &local_path) -> Codebase*;
};

CodebaseFromRoot::CodebaseFromRoot(Codebase *root)
  : m_root(root)
{
}

CodebaseFromRoot::~CodebaseFromRoot() {
  for (const auto &p : m_mounts) delete p.second;
  delete m_root;
}

void CodebaseFromRoot::mount(const std::string &name, Codebase *codebase) {
  if (name.find('/') != std::string::npos) throw std::runtime_error("invalid mount name");
  if (codebase) {
    if (get(name)) throw std::runtime_error("mount path already exists");
    if (list(name).size() > 0) throw std::runtime_error("mount path already exists");
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_mounts.find(name) != m_mounts.end()) {
      throw std::runtime_error("mount path already exists");
    }
    m_mounts[name] = codebase;
  } else {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto i = m_mounts.find(name);
    if (i != m_mounts.end()) {
      delete i->second;
      m_mounts.erase(i);
    }
  }
}

auto CodebaseFromRoot::list(const std::string &path) -> std::list<std::string> {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::string local_path;
  if (auto codebase = find_mount(path, local_path)) {
    return codebase->list(local_path);
  }
  auto list = m_root->list(path);
  if (path == "/") for (const auto &p : m_mounts) list.push_back(p.first + '/');
  return list;
}

auto CodebaseFromRoot::get(const std::string &path) -> SharedData* {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::string local_path;
  if (auto codebase = find_mount(path, local_path)) {
    return codebase->get(local_path);
  }
  return m_root->get(path);
}

void CodebaseFromRoot::set(const std::string &path, SharedData *data) {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::string local_path;
  if (auto codebase = find_mount(path, local_path)) {
    return codebase->set(local_path, data);
  }
  return m_root->set(path, data);
}

auto CodebaseFromRoot::watch(const std::string &path, const std::function<void(const std::list<std::string> &)> &on_update) -> Watch* {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::string norm_path = utils::path_normalize(path);
  std::string local_path;
  if (auto codebase = find_mount(norm_path, local_path)) {
    std::string base_path = norm_path.substr(0, norm_path.length() - local_path.length());
    return codebase->watch(local_path, [=](const std::list<std::string> &filenames) {
      std::list<std::string> list;
      for (const auto &filename : filenames) list.push_back(utils::path_join(base_path, filename));
      on_update(list);
    });
  }
  return m_root->watch(path, on_update);
}

auto CodebaseFromRoot::find_mount(const std::string &path, std::string &local_path) -> Codebase* {
  std::string dirname = path;
  if (dirname.empty()) return nullptr;
  if (dirname.front() != '/') dirname = '/' + dirname;
  if (dirname.back() != '/') dirname += '/';
  for (auto &p : m_mounts) {
    auto &name = p.first;
    auto n = name.length();
    if (dirname.length() >= n + 2) {
      if (dirname[n + 1] == '/') {
        if (!std::strncmp(dirname.c_str() + 1, name.c_str(), n)) {
          local_path = dirname.substr(n + 2);
          return p.second;
        }
      }
    }
  }
  return nullptr;
}

//
// CodebaseFromFS
//

class CodebaseFromFS : public Codebase {
public:
  CodebaseFromFS(const std::string &path);
  CodebaseFromFS(const std::string &path, const std::string &script);
  virtual ~CodebaseFromFS() override;

  virtual bool writable() const override { return true; }
  virtual auto entry() const -> const std::string& override { return m_entry; }
  virtual void entry(const std::string &path) override { m_entry = path; }
  virtual void mount(const std::string &path, Codebase *codebase) override;
  virtual auto list(const std::string &path) -> std::list<std::string> override;
  virtual auto get(const std::string &path) -> SharedData* override;
  virtual void set(const std::string &path, SharedData *data) override;
  virtual auto watch(const std::string &path, const std::function<void(const std::list<std::string> &)> &on_update) -> Watch* override;

private:
  struct WatchedFile {
    double time;
    std::set<pjs::Ref<Watch>> watches;
  };

  struct WatchedDir {
    std::map<std::string, double> times;
    std::set<pjs::Ref<Watch>> watches;
  };

  std::mutex m_mutex;
  std::string m_version;
  std::string m_base;
  std::string m_entry;
  std::string m_script;
  std::map<std::string, WatchedFile> m_watched_files;
  std::map<std::string, WatchedDir> m_watched_dirs;
  std::thread m_watch_thread;
  Net* m_watch_net = nullptr;

  void start_watching();
  void list_file_times(const std::string &path, std::map<std::string, double> &times);
};

CodebaseFromFS::CodebaseFromFS(const std::string &path) {
  auto full_path = fs::abs_path(path);

  if (!fs::exists(full_path)) {
    std::string msg("file or directory does not exist: ");
    throw std::runtime_error(msg + full_path);
  }

  m_base = full_path;

  if (!fs::is_dir(full_path)) {
    m_base = utils::path_dirname(full_path);
    m_entry = '/' + utils::path_basename(full_path);
  }

  start_watching();
}

CodebaseFromFS::CodebaseFromFS(const std::string &path, const std::string &script) {
  m_base = fs::abs_path(path);
  m_script = script;

  if (!fs::exists(m_base)) {
    std::string msg("file or directory does not exist: ");
    throw std::runtime_error(msg + m_base);
  }

  start_watching();
}

CodebaseFromFS::~CodebaseFromFS() {
  if (m_watch_net) m_watch_net->stop();
  if (m_watch_thread.joinable()) m_watch_thread.join();
}

void CodebaseFromFS::mount(const std::string &, Codebase *) {
  throw std::runtime_error("mounting unsupported");
}

auto CodebaseFromFS::list(const std::string &path) -> std::list<std::string> {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::list<std::string> list;
  auto full_path = utils::path_join(m_base, path);
  fs::read_dir(full_path, list);
  return list;
}

auto CodebaseFromFS::get(const std::string &path) -> SharedData* {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (path.empty() && !m_script.empty()) {
    Data buf(m_script, &s_dp);
    return SharedData::make(buf)->retain();
  } else {
    std::vector<uint8_t> data;
    auto norm_path = utils::path_normalize(path);
    auto full_path = utils::path_join(m_base, norm_path);
    if (!fs::is_file(full_path)) return nullptr;
    if (!fs::read_file(full_path, data)) return nullptr;
    if (data.empty()) return SharedData::make(Data());
    Data buf(&data[0], data.size(), &s_dp);
    return SharedData::make(buf)->retain();
  }
}

void CodebaseFromFS::set(const std::string &path, SharedData *data) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (data) {
    auto segs = utils::split(path, '/');
    if (segs.size() > 1) {
      auto path = m_base;
      segs.pop_back();
      for (const auto &s : segs) {
        path = utils::path_join(path, s);
        if (!fs::exists(path)) {
          fs::make_dir(path);
        } else if (!fs::is_dir(path)) {
          return;
        }
      }
    }
    auto norm_path = utils::path_normalize(path);
    auto full_path = utils::path_join(m_base, norm_path);
    Data buf(*data);
    fs::write_file(full_path, buf.to_bytes());
  } else {
    auto full_path = utils::path_join(m_base, path);
    fs::unlink(full_path);
  }
}

auto CodebaseFromFS::watch(const std::string &path, const std::function<void(const std::list<std::string> &)> &on_update) -> Watch* {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto w = new Watch(on_update);
  if (path.empty() || path.back() == '/') {
    std::map<std::string, double> times;
    std::string norm_path = utils::path_normalize(path);
    auto i = m_watched_dirs.find(norm_path);
    if (i == m_watched_dirs.end()) {
      auto &wd = m_watched_dirs[norm_path];
      list_file_times(norm_path, wd.times);
      wd.watches.insert(w);
    } else {
      i->second.watches.insert(w);
    }
  } else {
    auto norm_path = utils::path_normalize(path);
    auto full_path = utils::path_join(m_base, norm_path);
    auto i = m_watched_files.find(norm_path);
    if (i == m_watched_files.end()) {
      auto &wf = m_watched_files[norm_path];
      wf.time = fs::get_file_time(full_path);
      wf.watches.insert(w);
    } else {
      i->second.watches.insert(w);
    }
  }
  return w;
}

void CodebaseFromFS::start_watching() {
  std::condition_variable start_cv;
  std::mutex start_cv_mutex;
  std::unique_lock<std::mutex> lock(start_cv_mutex);
  m_watch_thread = std::thread(
    [&]() {
      {
        std::lock_guard<std::mutex> lock(start_cv_mutex);
        m_watch_net = &Net::current();
        start_cv.notify_one();
      }
      Timer timer;
      std::function<void()> check;
      check = [&]() {
        timer.schedule(1, [&]() {
          std::lock_guard<std::mutex> lock(m_mutex);
          for (auto &p : m_watched_files) {
            auto &path = p.first;
            auto &file = p.second;
            auto &watches = file.watches;
            auto i = watches.begin();
            while (i != watches.end()) {
              const auto w = i++;
              if ((*w)->closed()) {
                watches.erase(w);
              }
            }
            if (!watches.empty()) {
              auto norm_path = utils::path_normalize(path);
              auto full_path = utils::path_join(m_base, norm_path);
              auto t = fs::get_file_time(full_path);
              if (t != file.time) {
                file.time = t;
                std::list<std::string> pathnames;
                pathnames.push_back(norm_path);
                for (const auto &w : watches) notify(w, pathnames);
              }
            }
          }
          for (auto &p : m_watched_dirs) {
            auto &path = p.first;
            auto &dir = p.second;
            auto &watches = dir.watches;
            auto i = watches.begin();
            while (i != watches.end()) {
              const auto w = i++;
              if ((*w)->closed()) {
                watches.erase(w);
              }
            }
            if (!watches.empty()) {
              std::map<std::string, double> times;
              std::list<std::string> changes;
              auto norm_path = utils::path_normalize(path);
              list_file_times(norm_path, times);
              for (const auto &old : dir.times) {
                if (times.find(old.first) == times.end()) {
                  changes.push_back(old.first);
                }
              }
              for (const auto &cur : times) {
                auto i = dir.times.find(cur.first);
                if (i != dir.times.end()) {
                  if (i->second == cur.second) continue;
                }
                changes.push_back(cur.first);
              }
              dir.times = std::move(times);
              if (changes.size() > 0) {
                for (const auto &w : watches) {
                  notify(w, changes);
                }
              }
            }
          }
          check();
        });
      };
      check();
      Net::current().run();
    }
  );

  start_cv.wait(lock, [&]() { return m_watch_net; });
}

void CodebaseFromFS::list_file_times(const std::string &path, std::map<std::string, double> &times) {
  std::function<void(const std::string &, std::map<std::string, double> &)> traverse;
  traverse = [&](const std::string &path, std::map<std::string, double> &times) {
    std::list<std::string> names;
    auto real_path = utils::path_join(m_base, path);
    fs::read_dir(real_path, names);
    for (const auto &name : names) {
      auto pathname = utils::path_join(path, name);
      if (name.back() == '/') {
        traverse(pathname, times);
      } else {
        auto t = fs::get_file_time(utils::path_join(m_base, pathname));
        times[pathname] = t;
      }
    }
  };
  traverse(path, times);
}

//
// CodebaseFromMemory
//

class CodebaseFromMemory : public Codebase {
public:
  CodebaseFromMemory(const std::string &entry);

  virtual bool writable() const override { return false; }
  virtual auto entry() const -> const std::string& override { return m_entry; }
  virtual void entry(const std::string &path) override { m_entry = path; }
  virtual void mount(const std::string &path, Codebase *codebase) override;
  virtual auto list(const std::string &path) -> std::list<std::string> override;
  virtual auto get(const std::string &path) -> SharedData* override;
  virtual void set(const std::string &path, SharedData *data) override;
  virtual auto watch(const std::string &path, const std::function<void(const std::list<std::string> &)> &on_update) -> Watch* override;

private:
  std::mutex m_mutex;
  std::string m_version;
  std::string m_entry;
  std::map<std::string, pjs::Ref<SharedData>> m_files;
};

CodebaseFromMemory::CodebaseFromMemory(const std::string &entry)
  : m_entry(entry)
{
}

void CodebaseFromMemory::mount(const std::string &, Codebase *) {
  throw std::runtime_error("mounting unsupported");
}

auto CodebaseFromMemory::list(const std::string &path) -> std::list<std::string> {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::set<std::string> names;
  auto n = path.length();
  for (const auto &i : m_files) {
    const auto &name = i.first;
    if (name.length() > n && name[n] == '/' && utils::starts_with(name, path)) {
      auto i = name.find('/', n + 1);
      if (i == std::string::npos) {
        names.insert(name.substr(n + 1));
      } else {
        names.insert(name.substr(n + 1, i - n));
      }
    }
  }
  std::list<std::string> list;
  for (const auto &name : names) list.push_back(name);
  return list;
}

auto CodebaseFromMemory::get(const std::string &path) -> SharedData* {
  std::string k = normalize_path(path);
  std::lock_guard<std::mutex> lock(m_mutex);
  auto i = m_files.find(k);
  if (i == m_files.end()) return nullptr;
  return i->second->retain();
}

void CodebaseFromMemory::set(const std::string &path, SharedData *data) {
  std::string k = normalize_path(path);
  std::lock_guard<std::mutex> lock(m_mutex);
  m_files[k] = data;
}

auto CodebaseFromMemory::watch(const std::string &path, const std::function<void(const std::list<std::string> &)> &on_update) -> Watch* {
  return new Watch(on_update);
}

//
// Codebase
//

std::map<std::string, Codebase*> Codebase::s_builtin_codebases;

auto Codebase::list_builtin() -> std::vector<std::string> {
  std::vector<std::string> list;
  load_builtin_codebases();
  for (const auto &p : s_builtin_codebases) {
    list.push_back(p.first);
  }
  return list;
}

Codebase* Codebase::make() {
  return new CodebaseFromMemory("/main.js");
}

Codebase* Codebase::from_root(Codebase *root) {
  return new CodebaseFromRoot(root);
}

Codebase* Codebase::from_fs(const std::string &path) {
  return new CodebaseFromFS(path);
}

Codebase* Codebase::from_fs(const std::string &path, const std::string &script) {
  return new CodebaseFromFS(path, script);
}

Codebase* Codebase::from_builtin(const std::string &path) {
  load_builtin_codebases();
  auto i = s_builtin_codebases.find(path);
  if (i == s_builtin_codebases.end()) return nullptr;
  return i->second;
}

void Codebase::load_builtin_codebases() {
  if (s_builtin_codebases.size() > 0) return;

  Data out;
  auto *decompressor = Decompressor::brotli(
    [&](Data &data) {
      out.push(std::move(data));
    }
  );

  decompressor->input(Data(s_codebases_br, sizeof(s_codebases_br), &s_dp));
  decompressor->finalize();

  size_t p = 0, size = out.size();
  auto buffer = new uint8_t[size];
  out.to_bytes((uint8_t *)buffer);

  auto eof = [&]() -> bool {
    return p >= size;
  };

  auto read_string = [&]() -> std::string {
    std::string s;
    while (!eof() && buffer[p]) {
      s += (char)buffer[p++];
    }
    p++;
    return s;
  };

  auto read_bytes = [&](size_t n) -> std::vector<uint8_t> {
    if (p + n > size) n = size - p;
    auto s = p; p += n;
    return std::vector<uint8_t>(buffer + s, buffer + p);
  };

  while (!eof()) {
    auto filename = read_string();
    auto size = std::stoi(read_string());
    auto data = read_bytes(size);
    auto i = filename.find('/', 1);
    if (i != std::string::npos) {
      i = filename.find('/', i + 1);
      if (i != std::string::npos) {
        auto codebase_name = filename.substr(0, i);
        auto codebase = s_builtin_codebases[codebase_name];
        if (!codebase) {
          codebase = new CodebaseFromMemory("/main.js");
          s_builtin_codebases[codebase_name] = codebase;
        }
        codebase->set(filename.substr(i), SharedData::make(Data(data, &s_dp)));
      }
    }
  }

  delete [] buffer;
}

auto Codebase::normalize_path(const std::string &path) -> std::string {
  std::string k = path;
  if (k.empty() || k.front() != '/') {
    return utils::path_normalize('/' + k);
  } else {
    return utils::path_normalize(k);
  }
}

} // namespace pipy
