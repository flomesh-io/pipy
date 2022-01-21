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

#include "admin-service.hpp"
#include "codebase.hpp"
#include "api/json.hpp"
#include "filters/http.hpp"
#include "filters/tls.hpp"
#include "listener.hpp"
#include "worker.hpp"
#include "module.hpp"
#include "status.hpp"
#include "graph.hpp"
#include "fs.hpp"
#include "logging.hpp"

#ifdef PIPY_USE_GUI
#include "gui.tar.h"
#endif

namespace pipy {

static Data::Producer s_dp("Codebase Service");
static std::string s_server_name("pipy-repo");

AdminService::AdminService(CodebaseStore *store)
  : m_store(store)
#ifdef PIPY_USE_GUI
  , m_www_files((const char *)s_gui_tar, sizeof(s_gui_tar))
#else
  , m_www_files(nullptr, 0)
#endif
{
  auto create_response_head = [](const std::string &content_type) -> http::ResponseHead* {
    auto head = http::ResponseHead::make();
    auto headers = pjs::Object::make();
    headers->ht_set("server", s_server_name);
    headers->ht_set("content-type", content_type);
    head->headers(headers);
    return head;
  };

  auto create_response = [](int status) -> Message* {
    auto head = http::ResponseHead::make();
    auto headers = pjs::Object::make();
    headers->ht_set("server", s_server_name);
    head->headers(headers);
    head->status(status);
    return Message::make(head, nullptr);
  };

  m_response_head_text = create_response_head("text/plain");
  m_response_head_json = create_response_head("application/json");
  m_response_ok = create_response(200);
  m_response_created = create_response(201);
  m_response_deleted = create_response(204);
  m_response_not_found = create_response(404);
  m_response_method_not_allowed = create_response(405);

  // No repo, running a fixed codebase
  if (!store) {
    m_current_program = "/";
  }
}

void AdminService::open(int port, const Options &options) {
  Log::info("[admin] Starting admin service...");

  PipelineDef *pipeline_def = PipelineDef::make(nullptr, PipelineDef::LISTEN, "Admin Service");
  PipelineDef *pipeline_def_inbound = nullptr;

  if (!options.cert || !options.key) {
    pipeline_def_inbound = pipeline_def;

  } else {
    auto opts = pjs::Object::make();
    auto certificate = pjs::Object::make();
    certificate->set("cert", options.cert.get());
    certificate->set("key", options.key.get());
    opts->set("certificate", certificate);
    opts->set("trusted", options.trusted.get());
    pipeline_def_inbound = PipelineDef::make(nullptr, PipelineDef::NAMED, "Admin Service TLS-Offloaded");
    pipeline_def->append(new tls::Server(opts))->add_sub_pipeline(pipeline_def_inbound);
  }

  pipeline_def_inbound->append(
    new http::Server(
      [this](Message *msg) {
        return handle(msg);
      }
    )
  );

  Listener::Options opts;
  opts.reserved = true;
  auto listener = Listener::get("::", port);
  listener->set_options(opts);
  listener->pipeline_def(pipeline_def);
  m_port = port;
}

void AdminService::close() {
  if (auto listener = Listener::get("::", m_port)) {
    listener->pipeline_def(nullptr);
  }
}

auto AdminService::handle(Message *req) -> Message* {
  static std::string prefix_repo("/repo/");
  static std::string prefix_api_v1_repo("/api/v1/repo/");
  static std::string prefix_api_v1_files("/api/v1/files/");
  static std::string header_accept("accept");
  static std::string text_html("text/html");

  auto head = req->head()->as<http::RequestHead>();
  auto body = req->body();
  auto method = head->method()->str();
  auto path = head->path()->str();

  try {

    if (m_store) {
      if (path == "/api/v1/dump-store") {
        m_store->dump();
        return m_response_ok;
      }

      // GET /repo
      if (path == "/repo") {
        if (method == "GET") {
          return repo_GET("");
        } else {
          return m_response_method_not_allowed;
        }
      }

      // HEAD|GET|POST /repo/[path]
      if (utils::starts_with(path, prefix_repo)) {
        pjs::Value accept;
        head->headers()->get(header_accept, accept);
        if (!accept.is_string() || accept.s()->str().find(text_html) == std::string::npos) {
          path = path.substr(prefix_repo.length() - 1);
          if (path == "/") path.clear();
          if (method == "HEAD") {
            return repo_HEAD(path);
          } else if (method == "GET") {
            return repo_GET(path);
          } else if (method == "POST") {
            return repo_POST(path, body);
          } else {
            return m_response_method_not_allowed;
          }
        }
      }

      // GET /api/v1/repo
      if (path == "/api/v1/repo") {
        if (method == "GET") {
          return api_v1_repo_GET("/");
        } else {
          return m_response_method_not_allowed;
        }
      }

      // GET|POST|DELETE /api/v1/repo/[path]
      if (utils::starts_with(path, prefix_api_v1_repo)) {
        path = path.substr(prefix_api_v1_repo.length() - 1);
        if (method == "GET") {
          return api_v1_repo_GET(path);
        } else if (method == "POST") {
          return api_v1_repo_POST(path, body);
        } else if (method == "PATCH") {
          return api_v1_repo_PATCH(path, body);
        } else if (method == "DELETE") {
          return api_v1_repo_DELETE(path);
        } else {
          return m_response_method_not_allowed;
        }
      }
    }

    // GET /api/v1/files
    if (path == "/api/v1/files") {
      if (method == "GET") {
        return api_v1_files_GET("/");
      } else if (method == "POST") {
        return api_v1_files_POST("/", body);
      } else {
        return m_response_method_not_allowed;
      }
    }

    // GET|POST /api/v1/files/[path]
    if (utils::starts_with(path, prefix_api_v1_files)) {
      path = utils::path_normalize(path.substr(prefix_api_v1_files.length() - 1));
      if (method == "GET") {
        return api_v1_files_GET(path);
      } else if (method == "POST") {
        return api_v1_files_POST(path, body);
      } else if (method == "DELETE") {
        return api_v1_files_DELETE(path);
      } else {
        return m_response_method_not_allowed;
      }
    }

    // GET|POST|DELETE /api/v1/program
    if (path == "/api/v1/program") {
      if (method == "GET") {
        return api_v1_program_GET();
      } else if (method == "POST") {
        return api_v1_program_POST(body);
      } else if (method == "DELETE") {
        return api_v1_program_DELETE();
      } else {
        return m_response_method_not_allowed;
      }
    }

    // GET /api/v1/status
    if (path == "/api/v1/status") {
      if (method == "GET") {
        return api_v1_status_GET();
      } else {
        return m_response_method_not_allowed;
      }
    }

    // GET /api/v1/log
    if (path == "/api/v1/log") {
      if (method == "GET") {
        return api_v1_log_GET(req);
      } else {
        return m_response_method_not_allowed;
      }
    }

    // POST /api/v1/graph
    if (path == "/api/v1/graph") {
      if (method == "POST") {
        return api_v1_graph_POST(body);
      } else {
        return m_response_method_not_allowed;
      }
    }

    // Static GUI content
    if (method == "GET") {
      http::File *f = nullptr;
#ifdef PIPY_USE_GUI
      if (path == "/home" || path == "/home/") path = "/home/index.html";
      if (utils::starts_with(path, prefix_repo)) path = "/repo/[...]/index.html";
      auto i = m_www_file_cache.find(path);
      if (i != m_www_file_cache.end()) f = i->second;
      if (!f) {
        f = http::File::from(&m_www_files, path);
        if (f) m_www_file_cache[path] = f;
      }
#endif
      if (f) {
        auto headers = req->head()->as<http::RequestHead>()->headers();
        pjs::Value v;
        if (headers) headers->ht_get("accept-encoding", v);
        return f->to_message(v.is_string() ? v.s() : pjs::Str::empty.get());
      } else {
        return m_response_not_found;
      }
    } else {
      return m_response_method_not_allowed;
    }

  } catch (std::runtime_error &err) {
    return response(500, err.what());
  }
}

Message* AdminService::repo_HEAD(const std::string &path) {
  Data buf;
  std::string version;
  if (m_store->find_file(path, buf, version)) {
    return Message::make(
      response_head(200, {
        { "etag", version },
        { "content-type", "text/plain" },
      }),
      Data::make(buf)
    );
  }
  return m_response_not_found;
}

Message* AdminService::repo_GET(const std::string &path) {
  Data buf;
  std::string version;
  if (m_store->find_file(path, buf, version)) {
    return Message::make(
      response_head(200, {
        { "etag", version },
        { "content-type", "text/plain" },
      }),
      Data::make(buf)
    );
  }
  std::set<std::string> list;
  auto prefix = path;
  if (prefix.empty() || prefix.back() != '/') prefix += '/';
  m_store->list_codebases(prefix, list);
  if (list.empty()) return m_response_not_found;
  std::stringstream ss;
  for (const auto &i : list) ss << i << "/\n";
  return Message::make(
    m_response_head_text,
    Data::make(ss.str(), &s_dp)
  );
}

Message* AdminService::repo_POST(const std::string &path, Data *data) {
  if (path.back() == '/') {
    auto name = path.substr(0, path.length() - 1);
    if (auto codebase = m_store->find_codebase(name)) {
      Status status;
      if (!status.from_json(*data)) return response(400, "Invalid JSON");
      auto &instances = m_instance_statuses[codebase->id()];
      instances[status.uuid] = std::move(status);
      return m_response_created;
    }
  }
  return m_response_method_not_allowed;
}

Message* AdminService::api_v1_repo_GET(const std::string &path) {
  std::string filename;

  // List all codebases
  if (path.empty() || path == "/") {
    std::set<std::string> list;
    m_store->list_codebases("", list);
    return response(list);

  // Get codebase file
  } else if (auto codebase = codebase_of(path, filename)) {
    Data buf;
    if (!codebase->get_file(filename, buf)) return m_response_not_found;
    return response(buf);

  // Get codebase info
  } else if (auto codebase = m_store->find_codebase(path)) {
    CodebaseStore::Codebase::Info info;
    std::set<std::string> derived, edit, erased, files, base_files;
    codebase->get_info(info);
    codebase->list_derived(derived);
    codebase->list_edit(edit);
    codebase->list_erased(erased);
    codebase->list_files(false, files);
    if (auto base = m_store->codebase(info.base)) {
      base->list_files(true, base_files);
    }

    std::stringstream ss;
    auto to_array = [&](const std::set<std::string> &items) {
      bool first = true;
      ss << '[';
      for (const auto &i : items) {
        if (first) first = false; else ss << ',';
        ss << '"' << utils::escape(i) << '"';
      }
      ss << ']';
    };

    ss << "{\"version\":" << info.version;
    ss << ",\"path\":\"" << utils::escape(info.path) << '"';
    ss << ",\"main\":\"" << utils::escape(info.main) << '"';
    ss << ",\"files\":"; to_array(files);
    ss << ",\"editFiles\":"; to_array(edit);
    ss << ",\"erasedFiles\":"; to_array(erased);
    ss << ",\"baseFiles\":"; to_array(base_files);
    ss << ",\"derived\":"; to_array(derived);

    if (!info.base.empty()) {
      if (auto base = m_store->codebase(info.base)) {
        CodebaseStore::Codebase::Info info;
        base->get_info(info);
        ss << ",\"base\":\"" << utils::escape(info.path) << '"';
      }
    }

    ss << ",\"instances\":{";
    auto &instances = m_instance_statuses[codebase->id()];
    bool first = true;
    for (const auto &i : instances) {
      if (first) first = false; else ss << ',';
      ss << '"' << utils::escape(i.first) << "\":";
      i.second.to_json(ss);
    }
    ss << "}}";

    return Message::make(
      m_response_head_json,
      Data::make(ss.str(), &s_dp)
    );

  // Not found
  } else {
    return m_response_not_found;
  }
}

Message* AdminService::api_v1_repo_POST(const std::string &path, Data *data) {
  if (path.empty() || path.back() == '/') {
    return response(400, "Invalid codebase or filename");
  }

  std::string filename;

  // Create codebase file
  if (auto codebase = codebase_of(path, filename)) {
    codebase->set_file(filename, *data);
    return m_response_created;
  }

  pjs::Value json, base_val, main_val, version_val;
  if (JSON::decode(*data, json)) {
    if (json.is_object()) {
      if (auto obj = json.o()) {
        obj->get("base", base_val);
        obj->get("main", main_val);
        obj->get("version", version_val);
      }
    }
  }

  CodebaseStore::Codebase *base = nullptr;
  if (!base_val.is_undefined()) {
    if (!base_val.is_string()) return response(400, "Invalid base codebase");
    base = m_store->find_codebase(base_val.s()->str());
    if (!base) return response(400, "Base codebase not found");
  }

  std::string main;
  if (!main_val.is_undefined()) {
    if (!main_val.is_string()) return response(400, "Invalid main filename");
    main = main_val.s()->str();
  }

  int version = -1;
  if (!version_val.is_undefined()) {
    if (!version_val.is_number()) return response(400, "Invalid version number");
    version = version_val.n();
  }

  // Commit codebase edit
  if (auto codebase = m_store->find_codebase(path)) {
    CodebaseStore::Codebase::Info info;
    codebase->get_info(info);
    if (!main.empty() && main != info.main) codebase->set_main(main);
    if (version >= 0 && version != info.version) codebase->commit(version);
    return m_response_created;

  // Create codebase
  } else {
    m_store->make_codebase(path, version, base);
    return m_response_created;
  }
}

Message* AdminService::api_v1_repo_PATCH(const std::string &path, Data *data) {  
  if (path.empty() || path.back() == '/') {
    return response(400, "Invalid codebase or filename");
  }

  std::string filename;

  // Create codebase file
  if (auto codebase = codebase_of(path, filename)) {
    if (data->size() > 0) {
      codebase->set_file(filename, *data);
    } else {
      codebase->reset_file(filename);
    }
    return m_response_created;
  }

  return m_response_not_found;
}

Message* AdminService::api_v1_repo_DELETE(const std::string &path) {
  if (path.empty() || path.back() == '/') {
    return response(400, "Invalid codebase or filename");
  }

  std::string filename;

  // Delete codebase file
  if (auto codebase = codebase_of(path, filename)) {
    codebase->erase_file(filename);
    return m_response_deleted;
  }

  return m_response_not_found;
}

Message* AdminService::api_v1_files_GET(const std::string &path) {
  auto codebase = Codebase::current();
  if (path == "/") {
    std::set<std::string> collector;
    std::function<void(const std::string&)> list_dir;
    list_dir = [&](const std::string &path) {
      for (const auto &name : codebase->list(path)) {
        if (name.back() == '/') {
          auto sub = name.substr(0, name.length() - 1);
          auto str = path + '/' + sub;
          list_dir(str);
        } else {
          collector.insert(path + '/' + name);
        }
      }
    };
    std::string main;
    if (codebase) {
      list_dir("");
      main = codebase->entry();
    }
    std::stringstream ss;
    ss << '{';
    ss << '"' << "main" << '"' << ':';
    ss << '"' << utils::escape(main) << '"' << ',';
    ss << '"' << "files" << '"' << ':';
    ss << '[';
    bool first = true;
    for (const auto &path : collector) {
      if (first) first = false; else ss << ',';
      ss << '"';
      ss << utils::escape(path);
      ss << '"';
    }
    ss << ']';
    ss << ",\"readOnly\":";
    ss << (codebase && codebase->writable() ? "false" : "true");
    ss << '}';
    return Message::make(m_response_head_json, s_dp.make(ss.str()));
  } else {
    if (!codebase) return m_response_not_found;
    auto data = codebase->get(path);
    if (!data) return m_response_not_found;
    return response(*data);
  }
}

Message* AdminService::api_v1_files_POST(const std::string &path, Data *data) {
  auto codebase = Codebase::current();
  if (!codebase) return m_response_not_found;
  if (path == "/") {
    pjs::Value json, main;
    if (!JSON::decode(*data, json)) return response(400, "Invalid JSON");
    if (!json.is_object() || !json.o()) return response(400, "Invalid JSON object");
    json.o()->get("main", main);
    if (!main.is_string()) return response(400, "Invalid main filename");
    codebase->entry(main.s()->str());
    return m_response_created;
  } else {
    codebase->set(path, data);
    return m_response_created;
  }
}

Message* AdminService::api_v1_files_DELETE(const std::string &path) {
  auto codebase = Codebase::current();
  if (!codebase) return m_response_not_found;
  codebase->set(path, nullptr);
  return m_response_deleted;
}

Message* AdminService::api_v1_program_GET() {
  return response(m_current_program);
}

Message* AdminService::api_v1_program_POST(Data *data) {
  auto name = data->to_string();

  Codebase *old_codebase = Codebase::current();
  Codebase *new_codebase = nullptr;

  if (m_store) {
    if (name == "/") name = m_current_codebase;
    new_codebase = Codebase::from_store(m_store, name);
  } else if (name == "/") {
    m_current_codebase = "/";
    new_codebase = old_codebase;
  }

  if (!new_codebase) return response(400, "No codebase");

  auto &entry = new_codebase->entry();
  if (entry.empty()) {
    if (new_codebase != old_codebase) delete new_codebase;
    return response(400, "No main script");
  }

  if (new_codebase != old_codebase) {
    new_codebase->set_current();
  }

  pjs::Ref<Worker> old_worker = Worker::current();
  pjs::Ref<Worker> new_worker = Worker::make();
  if (new_worker->load_module(entry) && new_worker->start()) {
    if (old_worker) old_worker->stop();
    if (new_codebase != old_codebase) delete old_codebase;
    if (name != "/") m_current_codebase = name;
    m_current_program = m_current_codebase;
    Status::local.version = new_codebase->version();
    Status::local.update_modules();
    return m_response_created;
  } else {
    new_worker->stop();
    if (new_codebase != old_codebase) {
      if (old_codebase) {
        old_codebase->set_current();
        delete new_codebase;
      }
    }
    return response(400, "Failed to start up");
  }
}

Message* AdminService::api_v1_program_DELETE() {
  if (auto worker = Worker::current()) {
    worker->stop();
    Listener::for_each(
      [](Listener *l) {
        if (!l->reserved()) {
          l->pipeline_def(nullptr);
        }
      }
    );
    Status::local.update_modules();
  }
  m_current_program.clear();
  return m_response_deleted;
}

Message* AdminService::api_v1_status_GET() {
  std::stringstream ss;
  Status::local.to_json(ss);
  return Message::make(
    m_response_head_json,
    s_dp.make(ss.str())
  );
}

Message* AdminService::api_v1_graph_POST(Data *data) {
  Graph g;
  std::string error;
  if (!Graph::from_script(g, data->to_string(), error)) {
    return response(400, error);
  }

  std::stringstream ss;
  g.to_json(error, ss);
  return Message::make(
    m_response_head_json,
    s_dp.make(ss.str())
  );
}

Message* AdminService::api_v1_log_GET(Message *req) {
  std::string log_text;
  pjs::Value log_size;
  auto headers = req->head()->as<http::RequestHead>()->headers();
  if (headers) headers->ht_get("x-log-size", log_size);
  auto tail_size = Log::tail(log_size.to_number(), log_text);
  return Message::make(
    response_head(
      200,
      {
        { "server", s_server_name },
        { "content-type", "text/plain" },
        { "x-log-size", std::to_string(tail_size) },
      }
    ),
    s_dp.make(log_text)
  );
}

Message* AdminService::response(const std::set<std::string> &lines) {
  std::string str;
  for (const auto &line : lines) {
    if (!str.empty()) str += '\n';
    str += line;
  }
  return Message::make(
    m_response_head_text,
    s_dp.make(str)
  );
}

Message* AdminService::response(const Data &text) {
  return Message::make(
    m_response_head_text,
    Data::make(text)
  );
}

Message* AdminService::response(const std::string &text) {
  return response(Data(text, &s_dp));
}

Message* AdminService::response(int status_code, const std::string &message) {
  return Message::make(
    response_head(
      status_code,
      {
        { "server", s_server_name },
        { "content-type", "text/plain" },
      }
    ),
    s_dp.make(message)
  );
}

auto AdminService::codebase_of(const std::string &path) -> CodebaseStore::Codebase* {
  std::string filename;
  return codebase_of(path, filename);
}

auto AdminService::codebase_of(const std::string &path, std::string &filename) -> CodebaseStore::Codebase* {
  if (path.empty()) return nullptr;
  if (path.back() == '/') return nullptr;
  auto codebase_path = path;
  for (;;) {
    auto p = codebase_path.find_last_of('/');
    if (!p || p == std::string::npos) break;
    codebase_path = codebase_path.substr(0, p);
    if (auto codebase = m_store->find_codebase(codebase_path)) {
      filename = path.substr(codebase_path.length());
      return codebase;
    }
  }
  return nullptr;
}

auto AdminService::response_head(
  int status,
  const std::map<std::string, std::string> &headers
) -> http::ResponseHead* {
  auto head = http::ResponseHead::make();
  auto headers_obj = pjs::Object::make();
  headers_obj->ht_set("server", s_server_name);
  for (const auto &i : headers) headers_obj->ht_set(i.first, i.second);
  head->headers(headers_obj);
  head->status(status);
  return head;
}

} // namespace pipy
