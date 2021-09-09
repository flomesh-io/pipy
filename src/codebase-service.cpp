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

#include "codebase-service.hpp"
#include "api/json.hpp"
#include "filters/http.hpp"
#include "listener.hpp"
#include "worker.hpp"
#include "module.hpp"
#include "graph.hpp"
#include "logging.hpp"

#ifdef PIPY_USE_GUI
#include "gui.tar.h"
#endif

namespace pipy {

static Data::Producer s_dp("Codebase Service");

CodebaseService::CodebaseService(CodebaseStore *store)
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
    headers->ht_set("content-type", content_type);
    head->headers(headers);
    return head;
  };

  auto create_response = [](int status) -> Message* {
    auto head = http::ResponseHead::make();
    head->status(status);
    return Message::make(head, nullptr);
  };

  m_response_head_text = create_response_head("text/plain");
  m_response_head_json = create_response_head("application/json");
  m_response_ok = create_response(200);
  m_response_created = create_response(201);
  m_response_not_found = create_response(404);
  m_response_method_not_allowed = create_response(405);
}

void CodebaseService::open(int port) {
  Log::info("[codebase] Starting codebase service...");
  auto pipeline = Pipeline::make(nullptr, Pipeline::LISTEN, "Codebase Service");
  pipeline->append(
    new http::Server(
      [this](Context*, Message *msg) {
        return handle(msg);
      }
    )
  );
  auto listener = Listener::make("0.0.0.0", port);
  listener->open(pipeline);
}

auto CodebaseService::handle(Message *req) -> Message* {
  static std::string prefix_repo("/repo/");
  static std::string prefix_api_v1_repo("/api/v1/repo/");
  static std::string prefix_api_v1_files("/api/v1/files/");

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

      // HEAD|GET /repo/[path]
      if (utils::starts_with(path, prefix_repo)) {
        path = path.substr(prefix_repo.length() - 1);
        if (path == "/") path.clear();
        if (method == "HEAD") {
          return repo_HEAD(path);
        } else if (method == "GET") {
          return repo_GET(path);
        } else {
          return m_response_method_not_allowed;
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
        } else if (method == "DELETE") {
          return api_v1_repo_DELETE(path);
        } else {
          return m_response_method_not_allowed;
        }
      }
    }

    // GET /api/v1/files
    if (path == "/api/v1/files") {
      return api_v1_files_GET("");
    }

    // GET|POST /api/v1/files/*
    if (utils::starts_with(path, prefix_api_v1_files)) {
      path = utils::path_normalize(path.substr(prefix_api_v1_files.length() - 1));
      if (method == "GET") {
        return api_v1_files_GET(path);
      } else if (method == "POST") {
        return api_v1_files_POST(path, body);
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

    // GET /api/config
    if (path == "/api/v1/config") {
      if (method == "GET") {
        return api_v1_config_GET();
      } else {
        return m_response_method_not_allowed;
      }
    }

    // POST /api/graph
    if (path == "/api/v1/graph") {
      if (method == "POST") {
        return api_v1_graph_POST(body);
      } else {
        return m_response_method_not_allowed;
      }
    }

    // GET /api/log
    if (path == "/api/v1/log") {
      if (method == "GET") {
        return api_v1_log_GET(req);
      } else {
        return m_response_method_not_allowed;
      }
    }

    // Static GUI content
    if (method == "GET") {
      http::File *f = nullptr;
#ifdef PIPY_USE_GUI
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

Message* CodebaseService::repo_HEAD(const std::string &path) {
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

Message* CodebaseService::repo_GET(const std::string &path) {
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
  return response(list);
}

Message* CodebaseService::api_v1_repo_GET(const std::string &path) {

  // List all codebases
  if (path.empty() || path == "/") {
    std::set<std::string> list;
    m_store->list_codebases("", list);
    return response(list);

  // Get codebase file
  } else if (auto codebase = codebase_of(path)) {
    CodebaseStore::Codebase::Info info;
    codebase->get_info(info);
    Data buf;
    auto file_path = path.substr(info.path.length());
    if (!codebase->get_file(file_path, buf)) return m_response_not_found;
    return response(buf);

  // Get codebase info
  } else if (auto codebase = m_store->find_codebase(path)) {
    CodebaseStore::Codebase::Info info;
    std::set<std::string> edit, files, base_files;
    codebase->get_info(info);
    codebase->list_edit(edit);
    codebase->list_files(false, files);
    if (auto base = m_store->codebase(info.base)) {
      base->list_files(true, base_files);
    }
    auto json = pjs::Object::make();
    auto json_files = pjs::Array::make();
    auto json_edit_files = pjs::Array::make();
    auto json_base_files = pjs::Array::make();
    json->set("version", info.version);
    json->set("path", info.path);
    json->set("main", info.main);
    json->set("files", json_files);
    json->set("editFiles", json_edit_files);
    json->set("baseFiles", json_base_files);
    for (const auto &path : files) json_files->push(path);
    for (const auto &path : edit) json_edit_files->push(path);
    for (const auto &path : base_files) json_base_files->push(path);
    if (!info.base.empty()) {
      if (auto base = m_store->codebase(info.base)) {
        CodebaseStore::Codebase::Info info;
        base->get_info(info);
        json->set("base", info.path);
      }
    }
    return response(json);

  // Not found
  } else {
    return m_response_not_found;
  }
}

Message* CodebaseService::api_v1_repo_POST(const std::string &path, Data *data) {
  if (path.empty() || path.back() == '/') {
    return response(400, "Invalid codebase or filename");
  }

  // Create codebase file
  if (auto codebase = codebase_of(path)) {
    CodebaseStore::Codebase::Info info;
    codebase->get_info(info);
    auto file_path = path.substr(info.path.length());
    if (file_path == "index.txt") return response(400, "Reserved filename");
    codebase->set_file(file_path, *data);
    return m_response_created;
  }

  pjs::Value json, base_val, version_val;
  if (JSON::decode(*data, json)) {
    if (json.is_object()) {
      if (auto obj = json.o()) {
        obj->get("base", base_val);
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

  if (!version_val.is_number()) return response(400, "Invalid version number");
  int version = version_val.n();

  // Commit codebase edit
  if (auto codebase = m_store->find_codebase(path)) {
    codebase->commit(version);
    return m_response_created;

  // Create codebase
  } else {
    m_store->make_codebase(path, version, base);
    return m_response_created;
  }
}

Message* CodebaseService::api_v1_repo_DELETE(const std::string &path) {
  return m_response_ok;
}

Message* CodebaseService::api_v1_files_GET(const std::string &path) {
  if (path.empty()) {
    std::string json;
    file_tree_to_json("", json);
    return Message::make(m_response_head_json, s_dp.make(json));
  } else {
    auto data = Codebase::current()->get(path);
    if (!data) return m_response_not_found;
    return response(*data);
  }
}

Message* CodebaseService::api_v1_files_POST(const std::string &path, Data *data) {
  Codebase::current()->set(path, data);
  return m_response_created;
}

Message* CodebaseService::api_v1_program_GET() {
  std::string filename;
  if (auto worker = Worker::current()) {
    filename = worker->root()->path();
  }
  return response(filename);
}

Message* CodebaseService::api_v1_program_POST(Data *data) {
  auto current_worker = Worker::current();
  auto filename = utils::path_normalize(data->to_string());
  auto worker = Worker::make();
  if (worker->load_module(filename) && worker->start()) {
    if (current_worker) current_worker->stop();
    return m_response_created;
  } else {
    worker->stop();
    return response(400, "File not found");
  }
}

Message* CodebaseService::api_v1_program_DELETE() {
  return m_response_method_not_allowed;
}

Message* CodebaseService::api_v1_config_GET() {
  std::map<std::string, std::set<Pipeline*>> modules;
  Pipeline::for_each([&](Pipeline *p) {
    if (auto mod = p->module()) {
      if (mod->worker() == Worker::current()) {
        auto &set = modules[mod->path()];
        set.insert(p);
      }
    }
  });

  std::stringstream ss;
  ss << '{';

  bool first = true;
  for (const auto &i : modules) {
    if (first) first = false; else ss << ',';
    ss << '"';
    ss << utils::escape(i.first);
    ss << "\":";
    Graph g;
    Graph::from_pipelines(g, i.second);
    std::string error;
    g.to_json(error, ss);
  }

  ss << '}';

  return Message::make(
    m_response_head_json,
    s_dp.make(ss.str())
  );
}

Message* CodebaseService::api_v1_graph_POST(Data *data) {
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

Message* CodebaseService::api_v1_log_GET(Message *req) {
  std::string log_text;
  pjs::Value log_size;
  auto headers = req->head()->as<http::RequestHead>()->headers();
  if (headers) headers->ht_get("x-log-size", log_size);
  auto tail_size = Log::tail(log_size.to_number(), log_text);
  auto head = http::ResponseHead::make();
  headers = pjs::Object::make();
  head->headers(headers);
  headers->ht_set("content-type", "text/plain");
  headers->ht_set("x-log-size", std::to_string(tail_size));
  return Message::make(head, s_dp.make(log_text));
}

Message* CodebaseService::response(const std::set<std::string> &lines) {
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

Message* CodebaseService::response(const Data &text) {
  return Message::make(
    m_response_head_text,
    Data::make(text)
  );
}

Message* CodebaseService::response(const std::string &text) {
  return response(Data(text, &s_dp));
}

Message* CodebaseService::response(const pjs::Ref<pjs::Object> &json) {
  Data buf;
  JSON::encode(json.get(), nullptr, 0, buf);
  return Message::make(
    m_response_head_json,
    Data::make(buf)
  );
}

Message* CodebaseService::response(int status_code, const std::string &message) {
  return Message::make(
    response_head(
      status_code,
      {{ "content-type", "text/plain" }}
    ),
    s_dp.make(message)
  );
}

auto CodebaseService::codebase_of(const std::string &path) -> CodebaseStore::Codebase* {
  if (path.empty()) return nullptr;
  if (path.back() == '/') return nullptr;
  auto codebase_path = path;
  for (;;) {
    auto p = codebase_path.find_last_of('/');
    if (!p || p == std::string::npos) break;
    codebase_path = codebase_path.substr(0, p);
    if (auto codebase = m_store->find_codebase(codebase_path)) {
      return codebase;
    }
  }
  return nullptr;
}

void CodebaseService::file_tree_to_json(const std::string &path, std::string &json) {
  json += '{';
  auto list = Codebase::current()->list(path);
  bool first = true;
  for (const auto &name : list) {
    if (first) first = false; else json += ',';
    if (name.back() == '/') {
      auto sub = name.substr(0, name.length() - 1);
      json += '"';
      json += utils::escape(sub);
      json += '"';
      json += ':';
      file_tree_to_json(path + '/' + sub, json);
    } else {
      json += '"';
      json += utils::escape(name);
      json += '"';
      json += ':';
      json += '"';
      json += '"';
    }
  }
  json += '}';
}

auto CodebaseService::response_head(
  int status,
  const std::map<std::string, std::string> &headers
) -> http::ResponseHead* {
  auto head = http::ResponseHead::make();
  auto headers_obj = pjs::Object::make();
  for (const auto &i : headers) headers_obj->ht_set(i.first, i.second);
  head->headers(headers_obj);
  head->status(status);
  return head;
}

} // namespace pipy