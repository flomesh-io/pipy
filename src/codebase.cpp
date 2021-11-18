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
#include "status.hpp"
#include "api/http.hpp"
#include "api/url.hpp"
#include "fetch.hpp"
#include "utils.hpp"
#include "logging.hpp"

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>

#include <fstream>
#include <sstream>

namespace pipy {

static Data::Producer s_dp("Codebase");
static const pjs::Ref<pjs::Str> s_etag(pjs::Str::make("etag"));
static const pjs::Ref<pjs::Str> s_date(pjs::Str::make("last-modified"));

Codebase* Codebase::s_current = nullptr;

//
// CodebaseFromFS
//

class CodebaseFromFS : public Codebase {
public:
  CodebaseFromFS(const std::string &path);

  virtual auto version() const -> const std::string& override { return m_version; }
  virtual bool writable() const override { return true; }
  virtual auto entry() const -> const std::string& override { return m_entry; }
  virtual void entry(const std::string &path) override { m_entry = path; }
  virtual auto list(const std::string &path) -> std::list<std::string> override;
  virtual auto get(const std::string &path) -> Data* override;
  virtual void set(const std::string &path, Data *data) override;
  virtual void sync(const Status &status, const std::function<void(bool)> &on_update) override;

private:
  virtual void activate() override {
    chdir(m_base.c_str());
  }

  std::string m_version;
  std::string m_base;
  std::string m_entry;
};

CodebaseFromFS::CodebaseFromFS(const std::string &path) {
  char full_path[PATH_MAX];
  realpath(path.c_str(), full_path);

  struct stat st;
  if (stat(full_path, &st)) {
    std::string msg("file or directory does not exist: ");
    throw std::runtime_error(full_path);
  }

  m_base = full_path;

  if (!S_ISDIR(st.st_mode)) {
    auto i = m_base.find_last_of("/\\");
    m_entry = m_base.substr(i);
    m_base = m_base.substr(0, i);
  }
}

auto CodebaseFromFS::list(const std::string &path) -> std::list<std::string> {
  std::list<std::string> list;
  auto full_path = utils::path_join(m_base, path);
  if (DIR *dir = opendir(full_path.c_str())) {
    while (auto *entry = readdir(dir)) {
      if (entry->d_name[0] == '.') continue;
      std::string name(entry->d_name);
      if (entry->d_type == DT_DIR) name += '/';
      list.push_back(name);
    }
    closedir(dir);
  }
  return list;
}

auto CodebaseFromFS::get(const std::string &path) -> Data* {
  auto full_path = utils::path_join(m_base, path);

  struct stat st;
  if (stat(full_path.c_str(), &st) || !S_ISREG(st.st_mode)) {
    return nullptr;
  }

  std::ifstream fs(full_path, std::ios::in);
  if (!fs.is_open()) return nullptr;

  auto data = Data::make();
  char buf[DATA_CHUNK_SIZE];
  while (!fs.eof()) {
    fs.read(buf, sizeof(buf));
    s_dp.push(data, buf, fs.gcount());
  }

  return data;
}

void CodebaseFromFS::set(const std::string &path, Data *data) {
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
    for (const auto &c : data->chunks()) {
      fs.write(std::get<0>(c), std::get<1>(c));
    }
  } else {
    auto full_path = utils::path_join(m_base, path);
    unlink(full_path.c_str());
  }
}

void CodebaseFromFS::sync(const Status &status, const std::function<void(bool)> &on_update) {
  if (m_version.empty()) {
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
  virtual auto get(const std::string &path) -> pipy::Data* override;
  virtual void set(const std::string &path, Data *data) override {}
  virtual void sync(const Status &status, const std::function<void(bool)> &on_update) override {}

private:
  std::string m_version;
  std::string m_entry;
  std::map<std::string, pjs::Ref<pipy::Data>> m_files;
};

CodebsaeFromStore::CodebsaeFromStore(CodebaseStore *store, const std::string &name) {
  auto codebase = store->find_codebase(name);
  if (!codebase) {
    std::string msg("Codebase not found: ");
    throw std::runtime_error(msg + name);
  }

  CodebaseStore::Codebase::Info info;
  codebase->get_info(info);
  m_version = std::to_string(info.version);
  m_entry = info.main;

  std::set<std::string> paths;
  codebase->list_files(true, paths);
  codebase->list_edit(paths);

  for (const auto &path : paths) {
    Data buf;
    codebase->get_file(path, buf);
    m_files[path] = Data::make(buf);
  }
}

auto CodebsaeFromStore::list(const std::string &path) -> std::list<std::string> {
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

auto CodebsaeFromStore::get(const std::string &path) -> pipy::Data* {
  auto k = path;
  if (k.front() != '/') k.insert(k.begin(), '/');
  auto i = m_files.find(k);
  if (i == m_files.end()) return nullptr;
  return i->second;
}

//
// CodebaseFromHTTP
//

class CodebaseFromHTTP : public Codebase {
public:
  CodebaseFromHTTP(const std::string &url);
  ~CodebaseFromHTTP();

private:
  virtual auto version() const -> const std::string& override { return m_etag; }
  virtual bool writable() const override { return false; }
  virtual auto entry() const -> const std::string& override { return m_entry; }
  virtual void entry(const std::string &path) override {}
  virtual auto list(const std::string &path) -> std::list<std::string> override;
  virtual auto get(const std::string &path) -> pipy::Data* override;
  virtual void set(const std::string &path, Data *data) override {}
  virtual void sync(const Status &status, const std::function<void(bool)> &on_update) override;

  pjs::Ref<URL> m_url;
  Fetch* m_fetch;
  bool m_downloaded = false;
  std::string m_etag;
  std::string m_date;
  std::string m_base;
  std::string m_root;
  std::string m_entry;
  std::map<std::string, pjs::Ref<pipy::Data>> m_files;
  std::map<std::string, pjs::Ref<pipy::Data>> m_dl_temp;
  std::list<std::string> m_dl_list;
  pjs::Ref<pjs::Object> m_request_header_post_status;

  void download(const std::function<void(bool)> &on_update);
  void download_next(const std::function<void(bool)> &on_update);
  void response_error(const char *method, const char *path, http::ResponseHead *head);
};

CodebaseFromHTTP::CodebaseFromHTTP(const std::string &url)
  : m_url(URL::make(pjs::Value(url).s()))
{
  Outbound::Options options;
  auto host = m_url->hostname()->str() + ':' + m_url->port()->str();
  m_fetch = new Fetch(host, options);

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

CodebaseFromHTTP::~CodebaseFromHTTP() {
  delete m_fetch;
}

auto CodebaseFromHTTP::list(const std::string &path) -> std::list<std::string> {
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

auto CodebaseFromHTTP::get(const std::string &path) -> pipy::Data* {
  auto k = path;
  if (k.front() != '/') k.insert(k.begin(), '/');
  auto i = m_files.find(k);
  if (i == m_files.end()) return nullptr;
  return i->second;
}

void CodebaseFromHTTP::sync(const Status &status, const std::function<void(bool)> &on_update) {
  if (m_fetch->busy()) return;

  std::stringstream ss;
  status.to_json(ss);

  // Post status
  (*m_fetch)(
    Fetch::POST,
    m_url->path(),
    m_request_header_post_status,
    Data::make(ss.str(), &s_dp),
    [=](http::ResponseHead *head, Data *body) {

      // Check updates
      (*m_fetch)(
        Fetch::HEAD,
        m_url->path(),
        nullptr,
        nullptr,
        [=](http::ResponseHead *head, Data *body) {
          if (!head || head->status() != 200) {
            response_error("HEAD", m_url->href()->c_str(), head);
            on_update(false);
            return;
          }

          pjs::Value etag, date;
          head->headers()->get(s_etag, etag);
          head->headers()->get(s_date, date);

          std::string etag_str;
          std::string date_str;
          if (etag.is_string()) etag_str = etag.s()->str();
          if (date.is_string()) date_str = date.s()->str();

          if (!m_downloaded || etag_str != m_etag || date_str != m_date) {
            download(on_update);
          } else {
            m_fetch->close();
          }
        }
      );

    }
  );
}

void CodebaseFromHTTP::download(const std::function<void(bool)> &on_update) {
  (*m_fetch)(
    Fetch::GET,
    m_url->path(),
    nullptr,
    nullptr,
    [=](http::ResponseHead *head, Data *body) {
      if (!head || head->status() != 200) {
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
      head->headers()->get(s_etag, etag);
      head->headers()->get(s_date, date);
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
        m_files.clear();
        m_files[m_root] = body;
        m_fetch->close();
        m_entry = m_root;
        m_downloaded = true;
        on_update(true);
      }
    }
  );
}

void CodebaseFromHTTP::download_next(const std::function<void(bool)> &on_update) {
  if (m_dl_list.empty()) {
    m_files = std::move(m_dl_temp);
    m_fetch->close();
    m_downloaded = true;
    on_update(true);
    return;
  }

  auto name = m_dl_list.front();
  auto path = m_base + name;
  m_dl_list.pop_front();
  (*m_fetch)(
    Fetch::GET,
    pjs::Value(path).s(),
    nullptr,
    nullptr,
    [=](http::ResponseHead *head, Data *body) {
      if (!head || head->status() != 200) {
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

      m_dl_temp[name] = body;
      download_next(on_update);
    }
  );
}

void CodebaseFromHTTP::response_error(const char *method, const char *path, http::ResponseHead *head) {
  Log::error(
    "[codebase] %s %s -> %d %s",
    method, path,
    head->status(),
    head->status_text()->c_str()
  );
  m_fetch->close();
}

//
// Codebase
//

Codebase* Codebase::from_fs(const std::string &path) {
  return new CodebaseFromFS(path);
}

Codebase* Codebase::from_store(CodebaseStore *store, const std::string &name) {
  return new CodebsaeFromStore(store, name);
}

Codebase* Codebase::from_http(const std::string &url) {
  return new CodebaseFromHTTP(url);
}

} // namespace pipy
