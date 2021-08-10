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
#include "context.hpp"
#include "pipeline.hpp"
#include "session.hpp"
#include "api/http.hpp"
#include "api/url.hpp"
#include "filters/connect.hpp"
#include "filters/http.hpp"
#include "utils.hpp"
#include "logging.hpp"

#include <dirent.h>
#include <fstream>

namespace pipy {

static const pjs::Ref<pjs::Str> s_etag(pjs::Str::make("etag"));
static const pjs::Ref<pjs::Str> s_date(pjs::Str::make("last-modified"));

Codebase* Codebase::s_current = nullptr;

//
// CodebaseFS
//

CodebaseFS::CodebaseFS(const std::string &path) {
  struct stat st;
  if (stat(path.c_str(), &st)) {
    std::string msg("file or directory does not exist: ");
    throw std::runtime_error(msg + path);
  }

  if (S_ISDIR(st.st_mode)) {
    m_base = path;
  } else {
    auto i = path.find_last_of("/\\");
    m_base = path.substr(0, i);
    m_entry = path.substr(i);
  }
}

auto CodebaseFS::list(const std::string &path) -> std::list<std::string> {
  std::list<std::string> list;
  auto full_path = utils::path_join(m_base, path);
  if (DIR *dir = opendir(full_path.c_str())) {
    struct dirent entry;
    struct dirent *result;
    while (!readdir_r(dir, &entry, &result)) {
      if (!result) break;
      if (entry.d_name[0] == '.') continue;
      std::string name(entry.d_name);
      if (entry.d_type == DT_DIR) name += '/';
      list.push_back(name);
    }
    closedir(dir);
  }
  return list;
}

auto CodebaseFS::get(const std::string &path) -> Data* {
  static Data::Producer s_dp("CodebaseFS");

  auto full_path = utils::path_join(m_base, path);
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

void CodebaseFS::set(const std::string &path, Data *data) {
  auto full_path = utils::path_join(m_base, path);
  std::ofstream fs(full_path, std::ios::out | std::ios::trunc);
  if (!fs.is_open()) return;
  for (const auto &c : data->chunks()) {
    fs.write(std::get<0>(c), std::get<1>(c));
  }
}

void CodebaseFS::check(const std::function<void(bool)> &on_complete) {
  m_timer.schedule(0, [=]() {
    on_complete(false);
  });
}

void CodebaseFS::update(const std::function<void(bool)> &on_complete) {
  m_timer.schedule(0, [=]() {
    on_complete(true);
  });
}

//
// CodebaseHTTP
//

CodebaseHTTP::CodebaseHTTP(const std::string &url)
  : m_url(URL::make(pjs::Value(url).s()))
  , m_fetch(m_url->host())
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
}

CodebaseHTTP::~CodebaseHTTP() {
}

auto CodebaseHTTP::list(const std::string &path) -> std::list<std::string> {
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

void CodebaseHTTP::check(const std::function<void(bool)> &on_complete) {
  if (m_fetch.busy()) return;

  m_fetch(
    Fetch::HEAD,
    m_url->path(),
    nullptr,
    nullptr,
    [=](http::ResponseHead *head, Data *body) {
      if (head->status() != 200) {
        Log::error(
          "[codebase] HEAD %s -> %d %s",
          m_url->href()->c_str(),
          head->status(),
          head->status_text()->c_str()
        );
        m_fetch.close();
        on_complete(false);
        return;
      }

      pjs::Value etag, date;
      head->headers()->get(s_etag, etag);
      head->headers()->get(s_date, date);

      std::string etag_str;
      std::string date_str;
      if (etag.is_string()) etag_str = etag.s()->str();
      if (date.is_string()) date_str = date.s()->str();

      m_fetch.close();
      on_complete(
        etag_str != m_etag ||
        date_str != m_date
      );
    }
  );
}

void CodebaseHTTP::update(const std::function<void(bool)> &on_complete) {
  if (m_fetch.busy()) return;

  m_fetch(
    Fetch::GET,
    m_url->path(),
    nullptr,
    nullptr,
    [=](http::ResponseHead *head, Data *body) {
      if (head->status() != 200) {
        Log::error(
          "[codebase] GET %s -> %d %s",
          m_url->href()->c_str(),
          head->status(),
          head->status_text()->c_str()
        );
        m_fetch.close();
        on_complete(false);
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
        download_next(on_complete);
      } else {
        m_files.clear();
        m_files[m_root] = body;
        m_fetch.close();
        m_entry = m_root;
        on_complete(true);
      }
    }
  );
}

void CodebaseHTTP::download_next(const std::function<void(bool)> &on_complete) {
  if (m_dl_list.empty()) {
    m_files = std::move(m_dl_temp);
    m_fetch.close();
    on_complete(true);
    return;
  }

  auto path = m_base + m_dl_list.front();
  m_dl_list.pop_front();
  m_fetch(
    Fetch::GET,
    pjs::Value(path).s(),
    nullptr,
    nullptr,
    [=](http::ResponseHead *head, Data *body) {
      if (head->status() != 200) {
        Log::error(
          "[codebase] GET %s -> %d %s",
          path.c_str(),
          head->status(),
          head->status_text()->c_str()
        );
        m_fetch.close();
        on_complete(false);
        return;

      } else {
        Log::info(
          "[codebase] GET %s -> %d bytes",
          path.c_str(),
          body->size()
        );
      }

      m_dl_temp[path] = body;
      download_next(on_complete);
    }
  );
}

} // namespace pipy