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
#include "api/crypto.hpp"
#include "api/json.hpp"
#include "api/stats.hpp"
#include "filters/http.hpp"
#include "filters/tls.hpp"
#include "filters/websocket.hpp"
#include "gui-tarball.hpp"
#include "listener.hpp"
#include "worker.hpp"
#include "module.hpp"
#include "status.hpp"
#include "graph.hpp"
#include "compress.hpp"
#include "fs.hpp"
#include "log.hpp"

namespace pipy {

static Data::Producer s_dp("Codebase Service");
static std::string s_server_name("pipy-repo");

//
// AdminService
//

AdminService::AdminService(CodebaseStore *store)
  : m_store(store)
  , m_www_files(GuiTarball::data(), GuiTarball::size())
  , m_module(new Module())
{
  auto create_response_head = [](const std::string &content_type, bool gzip) -> http::ResponseHead* {
    auto head = http::ResponseHead::make();
    auto headers = pjs::Object::make();
    headers->ht_set("server", s_server_name);
    headers->ht_set("content-type", content_type);
    if (gzip) headers->ht_set("content-encoding", "gzip");
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

  m_response_head_text = create_response_head("text/plain", false);
  m_response_head_json = create_response_head("application/json", false);
  m_response_head_text_gzip = create_response_head("text/plain", true);
  m_response_head_json_gzip = create_response_head("application/json", true);
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

AdminService::~AdminService() {
  for (auto *i : m_instances) {
    delete i;
  }
}

void AdminService::open(int port, const Options &options) {
  Log::info("[admin] Starting admin service...");

  PipelineLayout *ppl = PipelineLayout::make(m_module);
  PipelineLayout *ppl_ws = PipelineLayout::make(m_module);
  PipelineLayout *ppl_inbound = nullptr;

  if (!options.cert || !options.key) {
    ppl_inbound = ppl;

  } else {
    tls::Server::Options opts;
    auto certificate = pjs::Object::make();
    certificate->set("cert", options.cert.get());
    certificate->set("key", options.key.get());
    opts.certificate = certificate;
    opts.trusted = options.trusted;
    ppl_inbound = PipelineLayout::make(m_module);
    ppl->append(new tls::Server(opts))->add_sub_pipeline(ppl_inbound);
  }

  ppl_inbound->append(
    new http::Server(
      [this](http::Server *server, Message *msg) {
        auto *ctx = static_cast<Context*>(server->context());
        return handle(ctx, msg);
      }
    )
  )->add_sub_pipeline(ppl_ws);

  ppl_ws->append(new websocket::Decoder());
  ppl_ws->append(new WebSocketHandler(this));
  ppl_ws->append(new websocket::Encoder());

  Listener::Options opts;
  opts.reserved = true;
  auto listener = Listener::get("::", port, Listener::Protocol::TCP);
  listener->set_options(opts);
  listener->pipeline_layout(ppl);
  m_port = port;

  metrics_history_step();
}

void AdminService::close() {
  if (auto listener = Listener::get("::", m_port, Listener::Protocol::TCP)) {
    listener->pipeline_layout()->shutdown();
    listener->pipeline_layout(nullptr);
  }
  m_metrics_history_timer.cancel();
}

void AdminService::write_log(const std::string &name, const Data &data) {
  auto i = m_local_log_watchers.find(name);
  if (i != m_local_log_watchers.end()) {
    for (auto *w : i->second) {
      w->send(data);
    }
  }
}

auto AdminService::handle(Context *ctx, Message *req) -> Message* {
  static std::string prefix_repo("/repo/");
  static std::string prefix_api_v1_repo("/api/v1/repo/");
  static std::string prefix_api_v1_files("/api/v1/files/");
  static std::string prefix_api_v1_metrics("/api/v1/metrics/");
  static std::string prefix_api_v1_log("/api/v1/log/");
  static std::string text_html("text/html");
  static pjs::ConstStr s_accept("accept");
  static pjs::ConstStr s_upgrade("upgrade");
  static pjs::ConstStr s_websocket("websocket");
  static pjs::ConstStr s_connection("connection");
  static pjs::ConstStr s_sec_websocket_key("sec-websocket-key");
  static pjs::ConstStr s_sec_websocket_accept("sec-websocket-accept");

  auto head = req->head()->as<http::RequestHead>();
  auto body = req->body();
  auto method = head->method()->str();
  auto path = head->path()->str();

  pjs::Value accept, upgrade;
  head->headers()->get(s_accept, accept);
  head->headers()->get(s_upgrade, upgrade);
  bool is_browser = (accept.is_string() && accept.s()->str().find(text_html) != std::string::npos);
  bool is_websocket = (upgrade.is_string() && upgrade.s() == s_websocket);

  if (is_websocket) {
    pjs::Value sec_key;
    head->headers()->get(s_sec_websocket_key, sec_key);
    if (sec_key.is_string()) {
      auto headers = pjs::Object::make();
      auto head = http::ResponseHead::make();
      head->status(101);
      head->headers(headers);
      pjs::Ref<crypto::Hash> hash = crypto::Hash::make("sha1");
      hash->update(sec_key.s()->str() + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
      headers->set(s_sec_websocket_accept, hash->digest(Data::Encoding::Base64));
      headers->set(s_connection, s_upgrade.get());
      headers->set(s_upgrade, s_websocket.get());
      m_response_upgraded_ws = Message::make(head, nullptr);
    } else {
      m_response_upgraded_ws = m_response_ok;
    }
  } else {
    m_response_upgraded_ws = nullptr;
  }

  try {

    // GET /metrics
    if (path == "/metrics") {
      if (method == "GET") {
        return metrics_GET(head->headers());
      } else {
        return m_response_method_not_allowed;
      }
    }

    // GET /api/v1/metrics
    if (path == "/api/v1/metrics") {
      if (method == "GET") {
        return api_v1_metrics_GET("");
      } else {
        return m_response_method_not_allowed;
      }
    }

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
        if (is_websocket) {
          auto i = path.rfind('/');
          auto uuid = (i == std::string::npos ? path : path.substr(i + 1));
          ctx->instance_uuid = uuid;
          return m_response_upgraded_ws;
        } else if (!is_browser) {
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

    // GET /api/v1/metrics/[uuid]/[name]
    if (utils::starts_with(path, prefix_api_v1_metrics)) {
      if (method == "GET") {
        return api_v1_metrics_GET(path.substr(prefix_api_v1_metrics.length()));
      } else {
        return m_response_method_not_allowed;
      }
    }

    // GET /api/v1/log/[uuid]/[name]
    if (utils::starts_with(path, prefix_api_v1_log)) {
      if (is_websocket) {
        on_watch_start(ctx, path.substr(prefix_api_v1_log.length()));
        return m_response_upgraded_ws;
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

Message* AdminService::metrics_GET(pjs::Object *headers) {
  static pjs::ConstStr s_accept_encoding("accept-encoding");
  static std::string s_gzip("gzip");

  pjs::Value v;
  headers->get(s_accept_encoding, v);
  auto use_gzip = (v.is_string() && v.s()->str().find(s_gzip) != std::string::npos);

  Data data;
  Data::Builder db(data, &s_dp);
  auto db_input = [&](const void *data, size_t size) {
    db.push((const char *)data, size);
  };

  Compressor *compressor = nullptr;
  if (use_gzip) compressor = Compressor::gzip(db_input);

  char buf[DATA_CHUNK_SIZE];
  size_t buf_ptr = 0;

  auto output = [&](const void *data, size_t size) {
    if (compressor) {
      if (size == 1) {
        buf[buf_ptr++] = *(const char *)data;
        if (buf_ptr >= sizeof(buf)) {
          compressor->input(buf, buf_ptr, false);
          buf_ptr = 0;
        }
      } else {
        size_t p = 0;
        while (p < size) {
          auto n = std::min(sizeof(buf) - buf_ptr, size - p);
          std::memcpy(buf + buf_ptr, (const char *)data + p, n);
          p += n;
          buf_ptr += n;
          if (buf_ptr >= sizeof(buf)) {
            compressor->input(buf, buf_ptr, false);
            buf_ptr = 0;
          }
        }
      }
    } else {
      if (size == 1) {
        db.push(*(const char *)data);
      } else {
        db.push((const char *)data, size);
      }
    }
  };

  stats::Metric::local().collect_all();
  stats::Metric::local().to_prometheus(output, "");
  for (const auto inst : m_instances) {
    std::string inst_label("instance=\"");
    if (inst->status.name.empty()) {
      inst_label += std::to_string(inst->index);
    } else {
      inst_label += inst->status.name;
    }
    inst_label += '"';
    inst->metrics.to_prometheus(output, inst_label);
  }

  if (compressor) {
    compressor->input(buf, buf_ptr, true);
    compressor->end();
  }

  db.flush();

  return Message::make(
    use_gzip ? m_response_head_text_gzip : m_response_head_text,
    Data::make(std::move(data))
  );
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
      Instance *inst = get_instance(status.uuid);
      if (inst->status.uuid.empty()) {
        m_codebase_instances[codebase->id()].push_back(inst->index);
      }
      inst->status = std::move(status);
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
    auto &instances = m_codebase_instances[codebase->id()];
    bool first = true;
    for (const auto index : instances) {
      auto *inst = m_instances[index];
      if (first) first = false; else ss << ',';
      ss << '"' << inst->index << "\":";
      inst->status.to_json(ss);
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
    Status::local.update();
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
          l->pipeline_layout(nullptr);
        }
      }
    );
    Status::local.update();
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

Message* AdminService::api_v1_metrics_GET(const std::string &path) {
  stats::MetricSet *ms = nullptr;
  std::string uuid, name;
  if (!path.empty()) {
    auto i = path.find('/');
    if (i == std::string::npos) {
      name = path;
    } else {
      uuid = path.substr(0,i);
      name = path.substr(i+1);
    }
  }
  if (uuid.empty()) {
    ms = &stats::Metric::local();
  } else {
    auto i = m_instance_map.find(uuid);
    if (i != m_instance_map.end()) ms = &m_instances[i->second]->metrics;
  }
  if (!ms) return m_response_not_found;
  Data payload;
  ms->serialize_history(payload, name, m_metrics_timestamp);
  return Message::make(
    m_response_head_json,
    Data::make(payload)
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

auto AdminService::get_instance(const std::string &uuid) -> Instance* {
  auto i = m_instance_map.find(uuid);
  if (i != m_instance_map.end()) return m_instances[i->second];
  auto *inst = new Instance;
  inst->index = m_instances.size();
  m_instances.push_back(inst);
  m_instance_map[uuid] = inst->index;
  return inst;
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

void AdminService::on_watch_start(Context *ctx, const std::string &path) {
  auto i = path.find('/');
  if (i != std::string::npos) {
    ctx->instance_uuid = path.substr(0, i);
    ctx->log_name = path.substr(i + 1);
    ctx->log_watcher = new LogWatcher(this, ctx->instance_uuid, ctx->log_name);
    if (auto *inst = get_instance(ctx->instance_uuid)) {
      if (auto *admin_link = inst->admin_link) {
        admin_link->log_enable(ctx->log_name, true);
      }
    }
  }
}

void AdminService::on_log(Context *ctx, const std::string &name, const Data &data) {
  if (auto *inst = get_instance(ctx->instance_uuid)) {
    bool watching = false;
    auto i = inst->log_watchers.find(name);
    if (i != inst->log_watchers.end()) {
      for (auto *w : i->second) {
        w->send(data);
        watching = true;
      }
    }
    if (!watching) {
      if (auto admin_link = inst->admin_link) {
        admin_link->log_enable(name, false);
      }
    }
  }
}

void AdminService::on_metrics(Context *ctx, const Data &data) {
  stats::MetricSet::deserialize(
    data,
    [this](const std::string &uuid) -> stats::MetricSet* {
      return &get_instance(uuid)->metrics;
    }
  );
}

void AdminService::metrics_history_step() {
  m_metrics_timestamp = std::chrono::steady_clock::now();
  stats::Metric::local().collect_all();
  stats::Metric::local().history_step();
  for (auto *i : m_instances) {
    i->metrics.history_step();
  }
  m_metrics_history_timer.schedule(
    5, [this]() { metrics_history_step(); }
  );
}

//
// AdminService::WebSocketHandler
//

auto AdminService::WebSocketHandler::clone() -> Filter* {
  return new AdminService::WebSocketHandler(*this);
}

void AdminService::WebSocketHandler::reset() {
  Filter::reset();
  m_payload.clear();
  m_started = false;
  if (context()) {
    auto ctx = static_cast<Context*>(context());
    if (auto inst = m_service->get_instance(ctx->instance_uuid)) {
      if (inst->admin_link == this) {
        inst->admin_link = nullptr;
      }
    }
  }
}

void AdminService::WebSocketHandler::process(Event *evt) {
  if (auto start = evt->as<MessageStart>()) {
    auto msg = start->head()->as<websocket::MessageHead>();
    if (msg->opcode() == 8) {
      auto ctx = static_cast<Context*>(context());
      delete ctx->log_watcher;
      ctx->log_watcher = nullptr;
    }
    m_started = true;
    m_payload.clear();
  } else if (auto data = evt->as<Data>()) {
    if (m_started) {
      m_payload.push(*data);
    }
  } else if (evt->is<MessageEnd>()) {
    if (m_started) {
      Data buf;
      m_payload.shift_to(
        [](int b) { return b == '\n'; },
        buf
      );
      auto command = buf.to_string();
      auto ctx = static_cast<Context*>(context());
      if (auto inst = m_service->get_instance(ctx->instance_uuid)) {
        inst->admin_link = this;
      }
      static std::string s_log_prefix("log/");
      if (utils::starts_with(command, s_log_prefix)) {
        auto name = command.substr(s_log_prefix.length());
        name.pop_back(); // pop out the ending '\n'
        m_service->on_log(ctx, name, m_payload);
      } else if (command == "metrics\n") {
        m_service->on_metrics(ctx, m_payload);
      } else if (command == "watch\n") {
        if (auto *w = ctx->log_watcher) w->set_handler(this);
      }
      m_payload.clear();
      m_started = false;
    }
  } else if (evt->is<StreamEnd>()) {
    auto ctx = static_cast<Context*>(context());
    delete ctx->log_watcher;
    ctx->log_watcher = nullptr;
  }
}

void AdminService::WebSocketHandler::log_enable(const std::string &name, bool enabled) {
  static std::string s_on("log/on/");
  static std::string s_off("log/off/");
  auto body = (enabled ? s_on : s_off) + name;
  auto head = websocket::MessageHead::make();
  Filter::output(Message::make(head, body));
}

void AdminService::WebSocketHandler::log_broadcast(const Data &data) {
  auto head = websocket::MessageHead::make();
  Filter::output(Message::make(head, Data::make(data)));
}

void AdminService::WebSocketHandler::dump(Dump &d) {
  Filter::dump(d);
  d.name = "AdminService::WebSocketHandler";
}

//
// AdminService::LogWatcher
//

AdminService::LogWatcher::LogWatcher(AdminService *service, const std::string &uuid, const std::string &name)
  : m_service(service)
  , m_uuid(uuid)
  , m_name(name)
{
  if (m_uuid.empty()) {
    m_service->m_local_log_watchers[name].insert(this);
  } else if (auto *inst = m_service->get_instance(m_uuid)) {
    auto &watchers = inst->log_watchers[m_name];
    watchers.insert(this);
  }
}

AdminService::LogWatcher::~LogWatcher() {
  if (m_uuid.empty()) {
    auto &watchers = m_service->m_local_log_watchers[m_name];
    watchers.erase(this);
    if (watchers.empty()) m_service->m_local_log_watchers.erase(m_name);
  } else if (auto *inst = m_service->get_instance(m_uuid)) {
    auto &watchers = inst->log_watchers[m_name];
    watchers.erase(this);
    if (watchers.empty()) inst->log_watchers.erase(m_name);
  }
}

void AdminService::LogWatcher::set_handler(WebSocketHandler *handler) {
  m_handler = handler;
}

void AdminService::LogWatcher::send(const Data &data) {
  if (m_handler) {
    m_handler->log_broadcast(data);
  }
}

} // namespace pipy
