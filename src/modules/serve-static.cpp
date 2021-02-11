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

#include "serve-static.hpp"
#include "utils.hpp"

#include <ctime>
#include <dirent.h>
#include <fstream>
#include <iostream>

NS_BEGIN

//
// ServeStatic
//

std::string ServeStatic::s_root_path;

void ServeStatic::set_root_path(const std::string &path) {
  s_root_path = path;
}

ServeStatic::ServeStatic() {
}

ServeStatic::~ServeStatic() {
}

auto ServeStatic::help() -> std::list<std::string> {
  return {
    "Outputs files according to URIs in HTTP requests",
    "path = Root directory of files",
    "prefix = Context prefix for message info",
    "ext.<name> = Maps an extension name to its MIME type",
  };
}

void ServeStatic::config(const std::map<std::string, std::string> &params) {
  auto path = utils::get_param(params, "path", ".");
  auto prefix = utils::get_param(params, "prefix");

  std::map<std::string, std::string> mime_types;
  for (const auto &p : params) {
    const auto &k = p.first;
    const auto &v = p.second;
    if (std::strncmp(k.c_str(), "ext.", 4)) continue;
    mime_types[utils::lower(k.substr(4))] = v;
  }

  m_cache = std::make_shared<Cache>(path, mime_types);

  m_var_method = prefix + ".method";
  m_var_path = prefix + ".path";
  m_var_status_code = prefix + ".status_code";
  m_var_status = prefix + ".status";
  m_var_accept_encoding = prefix + ".request.accept-encoding";
  m_var_content_encoding = prefix + ".response.content-encoding";
  m_var_content_type = prefix + ".response.content-type";
  m_var_last_modified = prefix + ".response.last-modified";
}

auto ServeStatic::clone() -> Module* {
  auto clone = new ServeStatic();
  clone->m_cache = m_cache;
  clone->m_var_method = m_var_method;
  clone->m_var_path = m_var_path;
  clone->m_var_status_code = m_var_status_code;
  clone->m_var_status = m_var_status;
  clone->m_var_accept_encoding = m_var_accept_encoding;
  clone->m_var_content_encoding = m_var_content_encoding;
  clone->m_var_content_type = m_var_content_type;
  clone->m_var_last_modified = m_var_last_modified;
  return clone;
}

void ServeStatic::pipe(
  std::shared_ptr<Context> ctx,
  std::unique_ptr<Object> obj,
  Object::Receiver out
) {
  static std::string INDEX_HTML("index.html");

  if (obj->is<SessionStart>() || obj->is<SessionEnd>()) {
    out(std::move(obj));

  } else if (obj->is<MessageEnd>()) {
    std::string method, path;
    ctx->find(m_var_method, method);

    bool is_found = false;
    if (ctx->find(m_var_path, path)) {
      auto i = m_cache->files.find(path);
      if (i == m_cache->files.end()) {
        path = utils::path_join(path, INDEX_HTML);
        i = m_cache->files.find(path);
      }
      if (i != m_cache->files.end()) {
        is_found = true;
        if (method != "HEAD" && method != "GET") {
          ctx->variables[m_var_status_code] = "405";
          ctx->variables[m_var_status] = "Method Not Allowed";
          out(make_object<MessageStart>());
          out(make_object<MessageEnd>());
        } else {
          const auto &f = i->second;
          ctx->variables[m_var_status_code] = "200";
          ctx->variables[m_var_status] = "OK";
          ctx->variables[m_var_content_type] = f.content_type;
          ctx->variables[m_var_last_modified] = f.last_modified;
          out(make_object<MessageStart>());
          out(make_object<Data>(i->second.data));
          out(make_object<MessageEnd>());
        }
      }
    }

    if (!is_found) {
      ctx->variables[m_var_status_code] = "404";
      ctx->variables[m_var_status] = "Not Found";
      out(make_object<MessageStart>());
      out(make_object<MessageEnd>());
    }
  }
}

//
// ServeStatic::Cache
//

ServeStatic::Cache::Cache(const std::string &path, const std::map<std::string, std::string> mime_types) {
  auto list_dir = [](
    const std::string &path,
    std::function<void(const std::string&, bool)> cb
  ) {
    DIR *dir = opendir(path.c_str());
    if (!dir) {
      std::string msg("unable to open dir ");
      throw std::runtime_error(msg + path);
    }
    struct dirent entry;
    struct dirent *result;
    while (!readdir_r(dir, &entry, &result)) {
      if (!result) break;
      if (entry.d_name[0] == '.') continue;
      cb(std::string(entry.d_name), entry.d_type == DT_DIR);
    }
    closedir(dir);
  };

  auto root = !path.empty() && path.front() == '/' ? path : utils::path_join(s_root_path, path);

  std::function<void(const std::string&)> load_dir;
  load_dir = [&](const std::string &path) {
    auto dir_path = utils::path_join(root, path);
    list_dir(dir_path, [&](const std::string &name, bool is_dir) {
      auto key = utils::path_join(path, name);
      if (is_dir) {
        load_dir(key);
      } else {
        auto file_path = utils::path_join(dir_path, name);
        std::ifstream fs(file_path, std::ios::in);
        if (!fs.is_open()) {
          std::string msg("unable to open file ");
          throw std::runtime_error(msg + file_path);
        }

        static std::string OCTET_STREAM("application/octet-stream");
        auto p = file_path.rfind('.');
        auto content_type = OCTET_STREAM;
        if (p != std::string::npos) {
          auto i = mime_types.find(utils::lower(file_path.substr(p+1)));
          if (i != mime_types.end()) content_type = i->second;
        }

        auto t = (std::time_t)(utils::get_file_time(file_path) / 1000);
        auto tm = std::localtime(&t);
        char last_modified[100];
        std::strftime(last_modified, sizeof(last_modified), "%a, %d %b %Y %H:%M:%S GMT", tm);

        auto &file = files[key];
        file.content_type = content_type;
        file.last_modified = last_modified;

        std::cout << "[serve-static] Loading cache " << file_path << std::endl;
        char buf[1024];
        while (!fs.eof()) {
          fs.read(buf, sizeof(buf));
          file.data.push(buf, fs.gcount());
        }
      }
    });
  };

  load_dir("/");
}

NS_END
