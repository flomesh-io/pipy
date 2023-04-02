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
#include "codebase-store.hpp"
#include "context.hpp"
#include "pipeline.hpp"
#include "api/http.hpp"
#include "api/url.hpp"
#include "fetch.hpp"
#include "fs.hpp"
#include "utils.hpp"
#include "log.hpp"

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>

#include <fstream>
#include <sstream>
#include <mutex>

namespace pipy {

thread_local static Data::Producer s_dp("Codebase");
static const pjs::Ref<pjs::Str> s_etag(pjs::Str::make("etag"));
static const pjs::Ref<pjs::Str> s_date(pjs::Str::make("last-modified"));

Codebase* Codebase::s_current = nullptr;

//
// CodebaseFromFS
//

class CodebaseFromFS : public Codebase {
public:
  CodebaseFromFS(const std::string &path);
  CodebaseFromFS(const std::string &path, const std::string &script);

  virtual auto version() const -> const std::string& override { return m_version; }
  virtual bool writable() const override { return true; }
  virtual auto entry() const -> const std::string& override { return m_entry; }
  virtual void entry(const std::string &path) override { m_entry = path; }
  virtual auto list(const std::string &path) -> std::list<std::string> override;
  virtual auto get(const std::string &path) -> SharedData* override;
  virtual void set(const std::string &path, SharedData *data) override;
  virtual auto watch(const std::string &path, const std::function<void()> &on_update) -> Watch* override;
  virtual void sync(bool force, const std::function<void(bool)> &on_update) override;

private:
  virtual void activate() override {
    chdir(m_base.c_str());
  }

  std::mutex m_mutex;
  std::string m_version;
  std::string m_base;
  std::string m_entry;
  std::string m_script;
};

CodebaseFromFS::CodebaseFromFS(const std::string &path) {
  auto full_path = fs::abs_path(path);

  if (!fs::exists(full_path)) {
    std::string msg("file or directory does not exist: ");
    throw std::runtime_error(msg + full_path);
  }

  m_base = full_path;

  if (!fs::is_dir(full_path)) {
    auto i = m_base.find_last_of("/\\");
    m_entry = m_base.substr(i);
    m_base = m_base.substr(0, i);
  }
}

CodebaseFromFS::CodebaseFromFS(const std::string &path, const std::string &script) {
  m_base = fs::abs_path(path);
  m_script = script;

  if (!fs::exists(m_base)) {
    std::string msg("file or directory does not exist: ");
    throw std::runtime_error(msg + m_base);
  }
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
    auto full_path = utils::path_join(m_base, path);
    if (!fs::is_file(full_path)) return nullptr;
    if (!fs::read_file(full_path, data)) return nullptr;
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
        struct stat st;
        if (stat(path.c_str(), &st)) {
          mkdir(path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        } else if (!S_ISDIR(st.st_mode)) {
          return;
        }
      }
    }
    auto full_path = utils::path_join(m_base, path);
    std::ofstream fs(full_path, std::ios::out | std::ios::trunc);
    if (!fs.is_open()) return;
    Data buf(*data);
    for (const auto c : buf.chunks()) {
      fs.write(std::get<0>(c), std::get<1>(c));
    }
  } else {
    auto full_path = utils::path_join(m_base, path);
    unlink(full_path.c_str());
  }
}

auto CodebaseFromFS::watch(const std::string &path, const std::function<void()> &on_update) -> Watch* {
  return new Watch(on_update);
}

void CodebaseFromFS::sync(bool force, const std::function<void(bool)> &on_update) {
  if (force || m_version.empty()) {
    m_version = "1";
    on_update(true);
  }
}

//
// CodebsaeFromStore
//

class CodebsaeFromStore : public Codebase {
public:
  CodebsaeFromStore(CodebaseStore *store, const std::string &name);

  virtual auto version() const -> const std::string& override { return m_version; }
  virtual bool writable() const override { return false; }
  virtual auto entry() const -> const std::string& override { return m_entry; }
  virtual void entry(const std::string &path) override {}
  virtual auto list(const std::string &path) -> std::list<std::string> override;
  virtual auto get(const std::string &path) -> SharedData* override;
  virtual void set(const std::string &path, SharedData *data) override {}
  virtual auto watch(const std::string &path, const std::function<void()> &on_update) -> Watch* override;
  virtual void sync(bool force, const std::function<void(bool)> &on_update) override {}

private:
  std::mutex m_mutex;
  std::string m_version;
  std::string m_entry;
  std::map<std::string, pjs::Ref<SharedData>> m_files;
};

CodebsaeFromStore::CodebsaeFromStore(CodebaseStore *store, const std::string &name) {
  auto codebase = store->find_codebase(name);
  if (!codebase) {
    std::string msg("Codebase not found: ");
    throw std::runtime_error(msg + name);
  }

  CodebaseStore::Codebase::Info info;
  codebase->get_info(info);
  m_version = info.version;
  m_entry = info.main;

  std::set<std::string> paths;
  codebase->list_files(true, paths);
  codebase->list_edit(paths);

  for (const auto &path : paths) {
    Data buf;
    codebase->get_file(path, buf);
    m_files[path] = SharedData::make(buf);
  }
}

auto CodebsaeFromStore::list(const std::string &path) -> std::list<std::string> {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::set<std::string> names;
  auto n = path.length();
  for (const auto &i : m_files) {
    const auto &name = i.first;
    if (name.length() > path.length() &&
        name[n] == '/' && !std::strncmp(name.c_str(), path.c_str(), n)) {
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

auto CodebsaeFromStore::get(const std::string &path) -> SharedData* {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto k = path;
  if (k.front() != '/') k.insert(k.begin(), '/');
  auto i = m_files.find(k);
  if (i == m_files.end()) return nullptr;
  return i->second->retain();
}

auto CodebsaeFromStore::watch(const std::string &path, const std::function<void()> &on_update) -> Watch* {
  return new Watch(on_update);
}

//
// CodebaseFromHTTP
//

class CodebaseFromHTTP : public Codebase {
public:
  CodebaseFromHTTP(const std::string &url, const Fetch::Options &options);

private:
  virtual auto version() const -> const std::string& override { return m_etag; }
  virtual bool writable() const override { return false; }
  virtual auto entry() const -> const std::string& override { return m_entry; }
  virtual void entry(const std::string &path) override {}
  virtual auto list(const std::string &path) -> std::list<std::string> override;
  virtual auto get(const std::string &path) -> SharedData* override;
  virtual void set(const std::string &path, SharedData *data) override {}
  virtual auto watch(const std::string &path, const std::function<void()> &on_update) -> Watch* override;
  virtual void sync(bool force, const std::function<void(bool)> &on_update) override;

  struct WatchedFile {
    std::string etag;
    std::string date;
    std::set<pjs::Ref<Watch>> watches;
  };

  pjs::Ref<URL> m_url;
  Fetch m_fetch;
  bool m_downloaded = false;
  std::string m_etag;
  std::string m_date;
  std::string m_base;
  std::string m_root;
  std::string m_entry;
  std::map<std::string, pjs::Ref<SharedData>> m_files;
  std::map<std::string, pjs::Ref<SharedData>> m_dl_temp;
  std::map<std::string, WatchedFile> m_watched_files;
  std::list<std::string> m_dl_list;
  pjs::Ref<pjs::Object> m_request_header_post_status;
  std::mutex m_mutex;

  void download(const std::function<void(bool)> &on_update);
  void download_next(const std::function<void(bool)> &on_update);
  void watch_next();
  void response_error(const char *method, const char *path, http::ResponseHead *head);
};

CodebaseFromHTTP::CodebaseFromHTTP(const std::string &url, const Fetch::Options &options)
  : m_url(URL::make(pjs::Value(url).s()))
  , m_fetch(m_url->hostname()->str() + ':' + m_url->port()->str(), options)
{
  auto path = m_url->pathname()->str();
  auto i = path.find_last_of('/');
  if (i == std::string::npos) {
    m_base = '/';
    m_root = path;
  } else {
    m_base = path.substr(0, i);
    m_root = path.substr(i);
  }

  auto headers = pjs::Object::make();
  headers->set("content-type", "application/json");
  m_request_header_post_status = headers;
}

auto CodebaseFromHTTP::list(const std::string &path) -> std::list<std::string> {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::set<std::string> names;
  auto n = path.length();
  for (const auto &i : m_files) {
    const auto &name = i.first;
    if (name.length() > path.length() &&
        name[n] == '/' && !std::strncmp(name.c_str(), path.c_str(), n)) {
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

auto CodebaseFromHTTP::get(const std::string &path) -> SharedData* {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto k = path;
  if (k.front() != '/') k.insert(k.begin(), '/');
  auto i = m_files.find(k);
  if (i == m_files.end()) return nullptr;
  return i->second->retain();
}

auto CodebaseFromHTTP::watch(const std::string &path, const std::function<void()> &on_update) -> Watch* {
  auto w = new Watch(on_update);
  m_mutex.lock();
  auto &wf = m_watched_files[path];
  wf.watches.insert(w);
  m_mutex.unlock();
  return w;
}

void CodebaseFromHTTP::sync(bool force, const std::function<void(bool)> &on_update) {
  if (m_fetch.busy()) return;

  if (force) {
    download(on_update);
    return;
  }

  // Check updates
  m_fetch(
    Fetch::HEAD,
    m_url->path(),
    nullptr,
    nullptr,
    [=](http::ResponseHead *head, Data *body) {
      if (!head || head->status != 200) {
        response_error("HEAD", m_url->href()->c_str(), head);
        on_update(false);
        return;
      }

      pjs::Value etag, date;
      head->headers->get(s_etag, etag);
      head->headers->get(s_date, date);

      std::string etag_str;
      std::string date_str;
      if (etag.is_string()) etag_str = etag.s()->str();
      if (date.is_string()) date_str = date.s()->str();

      if (!m_downloaded || etag_str != m_etag || date_str != m_date) {
        download(on_update);
      } else {
        m_mutex.lock();
        m_dl_list.clear();
        for (auto &wf : m_watched_files) {
          auto &watches = wf.second.watches;
          auto i = watches.begin();
          while (i != watches.end()) {
            const auto w = i++;
            if ((*w)->closed()) {
              watches.erase(w);
            }
          }
          if (!watches.empty()) m_dl_list.push_back(wf.first);
        }
        m_mutex.unlock();
        watch_next();
      }
    }
  );
}

void CodebaseFromHTTP::download(const std::function<void(bool)> &on_update) {
  m_fetch(
    Fetch::GET,
    m_url->path(),
    nullptr,
    nullptr,
    [=](http::ResponseHead *head, Data *body) {
      if (!head || head->status != 200) {
        response_error("GET", m_url->href()->c_str(), head);
        on_update(false);
        return;

      } else {
        Log::info(
          "[codebase] GET %s -> %d bytes",
          m_url->href()->c_str(),
          body->size()
        );
      }

      pjs::Value etag, date;
      head->headers->get(s_etag, etag);
      head->headers->get(s_date, date);
      if (etag.is_string()) m_etag = etag.s()->str(); else m_etag.clear();
      if (date.is_string()) m_date = date.s()->str(); else m_date.clear();

      auto text = body->to_string();
      if (text.length() > 2 &&
          text[0] == '/' &&
          text[1] != '/' &&
          text[1] != '*'
      ) {
        m_dl_temp.clear();
        m_dl_list.clear();
        auto lines = utils::split(text, '\n');
        for (const auto &line : lines) {
          auto path = utils::trim(line);
          if (!path.empty()) m_dl_list.push_back(path);
        }
        m_entry = m_dl_list.front();
        download_next(on_update);
      } else {
        m_mutex.lock();
        m_files.clear();
        m_files[m_root] = SharedData::make(*body);
        m_entry = m_root;
        m_downloaded = true;
        m_mutex.unlock();
        m_fetch.close();
        on_update(true);
      }
    }
  );
}

void CodebaseFromHTTP::download_next(const std::function<void(bool)> &on_update) {
  if (m_dl_list.empty()) {
    m_mutex.lock();
    m_files = std::move(m_dl_temp);
    m_downloaded = true;
    for (auto &wf : m_watched_files) {
      wf.second.etag.clear();
      wf.second.date.clear();
    }
    m_mutex.unlock();
    m_fetch.close();
    on_update(true);
    return;
  }

  auto name = m_dl_list.front();
  auto path = m_base + name;
  m_dl_list.pop_front();
  m_fetch(
    Fetch::GET,
    pjs::Value(path).s(),
    nullptr,
    nullptr,
    [=](http::ResponseHead *head, Data *body) {
      if (!head || head->status != 200) {
        response_error("GET", path.c_str(), head);
        on_update(false);
        return;

      } else {
        Log::info(
          "[codebase] GET %s -> %d bytes",
          path.c_str(),
          body->size()
        );
      }

      m_dl_temp[name] = SharedData::make(body ? *body : Data());
      download_next(on_update);
    }
  );
}

void CodebaseFromHTTP::watch_next() {
  if (m_dl_list.empty()) {
    m_fetch.close();
    return;
  }

  auto name = m_dl_list.front();
  auto path = m_base + name;
  m_dl_list.pop_front();
  m_fetch(
    Fetch::HEAD,
    pjs::Value(path).s(),
    nullptr,
    nullptr,
    [=](http::ResponseHead *head, Data *body) {
      if (head && head->status == 200) {
        pjs::Value etag, date;
        head->headers->get(s_etag, etag);
        head->headers->get(s_date, date);

        std::string etag_str;
        std::string date_str;
        if (etag.is_string()) etag_str = etag.s()->str();
        if (date.is_string()) date_str = date.s()->str();

        m_mutex.lock();

        auto i = m_watched_files.find(name);
        if (i != m_watched_files.end()) {
          auto &wf = i->second;
          if (wf.etag.empty() && wf.date.empty()) {
            wf.etag = etag_str;
            wf.date = date_str;
          } else if (etag_str != wf.etag || date_str != wf.date) {
            m_fetch(
              Fetch::GET,
              pjs::Value(path).s(),
              nullptr,
              nullptr,
              [=](http::ResponseHead *head, Data *body) {
                if (head && head->status == 200) {
                  m_mutex.lock();
                  auto i = m_watched_files.find(name);
                  if (i != m_watched_files.end()) {
                    auto &wf = i->second;
                    pjs::Value etag, date;
                    head->headers->get(s_etag, etag);
                    head->headers->get(s_date, date);
                    if (etag.is_string()) wf.etag = etag.s()->str(); else wf.etag.clear();
                    if (date.is_string()) wf.date = date.s()->str(); else wf.date.clear();
                    m_files[name] = SharedData::make(body ? *body : Data());
                    for (const auto &w : wf.watches) notify(w);
                  }
                  m_mutex.unlock();
                } else {
                  response_error("GET", path.c_str(), head);
                }
                watch_next();
              }
            );
          }
        }

        m_mutex.unlock();

      } else {
        response_error("HEAD", path.c_str(), head);
      }

      if (!m_fetch.busy()) watch_next();
    }
  );
}

void CodebaseFromHTTP::response_error(const char *method, const char *path, http::ResponseHead *head) {
  Log::error(
    "[codebase] %s %s -> %d %s",
    method, path,
    head ? head->status : 0,
    head ? head->statusText->c_str() : "Empty"
  );
  m_fetch.close();
}

//
// Codebase
//

Codebase* Codebase::from_fs(const std::string &path) {
  return new CodebaseFromFS(path);
}

Codebase* Codebase::from_fs(const std::string &path, const std::string &script) {
  return new CodebaseFromFS(path, script);
}

Codebase* Codebase::from_store(CodebaseStore *store, const std::string &name) {
  return new CodebsaeFromStore(store, name);
}

Codebase* Codebase::from_http(const std::string &url, const Fetch::Options &options) {
  return new CodebaseFromHTTP(url, options);
}

} // namespace pipy
