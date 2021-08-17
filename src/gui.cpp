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

#include "gui.hpp"
#include "codebase.hpp"
#include "pipeline.hpp"
#include "listener.hpp"
#include "message.hpp"
#include "worker.hpp"
#include "module.hpp"
#include "graph.hpp"
#include "api/configuration.hpp"
#include "api/http.hpp"
#include "filters/http.hpp"
#include "pjs/pjs.hpp"
#include "logging.hpp"
#include "utils.hpp"

#ifdef PIPY_USE_GUI
#include "gui.tar.h"
#endif

#include <sstream>

namespace pipy {

static Data::Producer s_dp_gui("GUI");

//
// GuiService
//

class GuiService : public Filter {
public:
  GuiService(Tarball &www_files)
    : m_www_files(www_files)
  {
    auto create_response_head = [](const std::string &content_type) -> http::ResponseHead* {
      auto head = http::ResponseHead::make();
      auto headers = pjs::Object::make();
      headers->ht_set("content-type", content_type);
      return head;
    };

    auto create_response = [](int status) -> Message* {
      auto head = http::ResponseHead::make();
      head->status(status);
      return Message::make(head, nullptr);
    };

    m_response_head_text = create_response_head("text/plain");
    m_response_head_json = create_response_head("application/json");
    m_response_head_json_error = create_response_head("application/json");
    m_response_created = create_response(201);
    m_response_bad_request = create_response(400);
    m_response_not_found = create_response(404);
    m_response_method_not_allowed = create_response(405);

    m_response_head_json_error->status(400);
  }

private:
  ~GuiService() {}

  virtual void dump(std::ostream &out) override {}

  virtual auto clone() -> Filter* override {
    return new GuiService(m_www_files);
  }

  virtual void reset() override {
  }

  virtual void process(Context *ctx, Event *inp) override {
    if (auto e = inp->as<MessageStart>()) {
      m_head = e->head();
      m_body = Data::make();
      return;

    } else if (auto data = inp->as<Data>()) {
      if (m_body) {
        m_body->push(*data);
        return;
      }

    } else if (inp->is<MessageEnd>()) {
      if (m_body) {
        pjs::Ref<Message> req = Message::make(m_head, m_body);
        output(process(req));
        m_head = nullptr;
        m_body = nullptr;
        return;
      }
    }

    output(inp);
  }

  Tarball& m_www_files;
  std::map<std::string, pjs::Ref<http::File>> m_www_file_cache;
  pjs::Ref<pjs::Object> m_head;
  pjs::Ref<Data> m_body;
  pjs::Ref<http::ResponseHead> m_response_head_text;
  pjs::Ref<http::ResponseHead> m_response_head_json;
  pjs::Ref<http::ResponseHead> m_response_head_json_error;
  pjs::Ref<Message> m_response_created;
  pjs::Ref<Message> m_response_bad_request;
  pjs::Ref<Message> m_response_not_found;
  pjs::Ref<Message> m_response_method_not_allowed;

  auto process(Message *req) -> Message* {
    auto &method = req->head()->as<http::RequestHead>()->method()->str();
    auto &path = req->head()->as<http::RequestHead>()->path()->str();

    // GET /api/files
    if (path == "/api/files") {
      std::string json;
      file_tree_to_json("", json);
      return Message::make(m_response_head_json, s_dp_gui.make(json));

    // /api/files/*
    } else if (!std::strncmp(path.c_str(), "/api/files/", 11)) {
      auto filename = utils::path_normalize(path.substr(11));

      // GET /api/files/*
      if (method == "GET") {
        auto data = Codebase::current()->get(filename);
        if (!data) return m_response_not_found;
        return Message::make(m_response_head_text, data);

      // POST /api/files/*
      } else if (method == "POST") {
        auto data = req->body();
        Codebase::current()->set(filename, data);
        return m_response_created;

      // Unsupported method
      } else {
        return m_response_method_not_allowed;
      }

    // /api/program
    } else if (path == "/api/program") {

      // GET /api/program
      if (method == "GET") {
        std::string filename;
        if (auto worker = Worker::current()) {
          filename = worker->root()->path();
        }
        return Message::make(m_response_head_text, s_dp_gui.make(filename));

      // POST /api/program
      } else if (method == "POST") {
        auto current_worker = Worker::current();
        auto filename = utils::path_normalize(req->body()->to_string());
        auto worker = Worker::make();
        if (worker->load_module(filename) && worker->start()) {
          if (current_worker) current_worker->stop();
          return m_response_created;
        } else {
          worker->stop();
          return m_response_bad_request;
        }

      // DELETE /api/program
      } else if (method == "DELETE") {
        return m_response_method_not_allowed;

      // Unsupported method
      } else {
        return m_response_method_not_allowed;
      }

    // GET /api/config
    } else if (path == "/api/config") {
      if (method != "GET") return m_response_method_not_allowed;
      return Message::make(
        m_response_head_json,
        s_dp_gui.make(config_to_json())
      );

    // POST /api/graph
    } else if (path == "/api/graph") {
      if (method != "POST") return m_response_method_not_allowed;
      Graph g;
      auto err = code_to_graph(g, req->body()->to_string());
      if (err.empty()) {
        std::stringstream ss;
        g.to_json(err, ss);
        return Message::make(
          m_response_head_json,
          s_dp_gui.make(ss.str())
        );
      }
      return Message::make(
        m_response_head_json_error,
        s_dp_gui.make(
          std::string("{\"error\":\"") + utils::escape(err) + "\"}"
        )
      );

    // GET /api/log
    } else if (path == "/api/log") {
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
      return Message::make(head, s_dp_gui.make(log_text));

    // Static GUI content
    } else {
      if (method == "GET") {
        http::File *f = nullptr;
        auto i = m_www_file_cache.find(path);
        if (i != m_www_file_cache.end()) f = i->second;
        if (!f) {
          f = http::File::from(&m_www_files, path);
          if (f) m_www_file_cache[path] = f;
        }
        if (f) {
          auto headers = req->head()->as<http::RequestHead>()->headers();
          pjs::Value v;
          if (headers) headers->ht_get("accept-encoding", v);
          return f->to_message(v.is_string() ? v.s() : pjs::Str::empty.get());
        } else {
          return m_response_not_found;
        }
        return req;
      } else {
        return m_response_method_not_allowed;
      }
    }
  }

  //
  // Output directory tree as JSON
  //

  void file_tree_to_json(const std::string &path, std::string &json) {
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

  //
  // Output configuration graph as JSON
  //

  auto config_to_json() -> std::string {
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
      for (auto *pipeline : i.second) {
        Graph::Pipeline p;
        p.name = pipeline->name();
        for (auto &f : pipeline->filters()) {
          Graph::Filter gf;
          gf.name = f->draw(gf.links, gf.fork);
          p.filters.emplace_back(std::move(gf));
        }
        switch (pipeline->type()) {
          case Pipeline::NAMED:
            g.add_named_pipeline(std::move(p));
            break;
          case Pipeline::LISTEN:
          case Pipeline::TASK:
            g.add_root_pipeline(std::move(p));
            break;
        }
      }
      std::string error;
      g.to_json(error, ss);
    }

    ss << '}';
    return ss.str();
  }

  //
  // Expression tree to configuration reducer
  //

  class ConfigReducer : public pjs::Expr::Reducer {
  public:
    ConfigReducer(Graph &g) : m_g(g) {}

    void flush() {
      if (m_p) {
        if (m_pt == Pipeline::NAMED) {
          m_g.add_named_pipeline(std::move(*m_p));
        } else {
          m_g.add_root_pipeline(std::move(*m_p));
        }
        delete m_p;
        m_p = nullptr;
      }
    }

  private:
    Graph& m_g;
    Graph::Pipeline* m_p = nullptr;
    Pipeline::Type m_pt;
    int m_named_count = 0;
    int m_listen_count = 0;
    int m_task_count = 0;

    enum ConfigValueType {
      UNDEFINED,
      BOOLEAN,
      NUMBER,
      STRING,
      CONFIG_MAKER,
      CONFIG_OBJECT,
      CONFIG_METHOD,
    };

    class ConfigValue : public pjs::Pooled<ConfigValue, pjs::Expr::Reducer::Value> {
    public:
      ConfigValue(ConfigValueType t) : m_t(t) {}
      ConfigValue(ConfigValueType t, const std::string &s) : m_t(t), m_s(s) {}
      ConfigValue(bool b) : m_t(BOOLEAN), m_b(b) {}
      ConfigValue(double n) : m_t(NUMBER), m_n(n) {}
      ConfigValue(const std::string &s) : m_t(STRING), m_s(s) {}

      auto t() const -> ConfigValueType { return m_t; }
      auto b() const -> bool { return m_b; }
      auto n() const -> double { return m_n; }
      auto s() const -> const std::string& { return m_s; }

    private:
      ConfigValueType m_t;
      bool m_b = false;
      double m_n = 0;
      std::string m_s;
    };

    static auto cv(Value *v) -> ConfigValue* {
      return static_cast<ConfigValue*>(v);
    }

    virtual void free(Value *val) override {
      delete cv(val);
    }

    virtual Value* undefined() override {
      return new ConfigValue(UNDEFINED);
    }

    virtual Value* boolean(bool b) override {
      return new ConfigValue(b);
    }

    virtual Value* number(double n) override {
      return new ConfigValue(n);
    }

    virtual Value* string(const std::string &s) override {
      return new ConfigValue(s);
    }

    virtual Value* get(const std::string &name) override {
      if (name == "pipy") return new ConfigValue(CONFIG_MAKER);
      return undefined();
    }

    virtual Value* call(Value *fn, Value **argv, int argc) override {
      Value *ret = nullptr;
      if (cv(fn)->t() == CONFIG_MAKER) {
        ret = new ConfigValue(CONFIG_OBJECT);
      } else if (cv(fn)->t() == CONFIG_METHOD) {
        const auto &m = cv(fn)->s();
        if (m == "pipeline") {
          flush();
          m_pt = Pipeline::NAMED;
          m_p = new Graph::Pipeline;
          m_p->name = argc > 0 && cv(argv[0])->t() == STRING
            ? cv(argv[0])->s()
            : std::string("Pipeline #") + std::to_string(++m_named_count);

        } else if (m == "listen") {
          flush();
          m_pt = Pipeline::LISTEN;
          m_p = new Graph::Pipeline;
          m_p->name = argc > 0 && cv(argv[0])->t() == NUMBER
            ? std::string("Listen 0.0.0.0:") + std::to_string(int(cv(argv[0])->n()))
            : std::string("Listen #") + std::to_string(++m_listen_count);

        } else if (m == "task") {
          flush();
          m_pt = Pipeline::TASK;
          m_p = new Graph::Pipeline;
          m_p->name = argc > 0 && cv(argv[0])->t() == STRING
            ? std::string("Task every ") + cv(argv[0])->s()
            : std::string("Task #") + std::to_string(++m_task_count);

        } else if (m_p) {
          if (m == "link") {
            Graph::Filter f;
            f.name = m;
            for (int i = 0; i < argc; i += 2) {
              if (cv(argv[i])->t() == STRING) {
                f.links.push_back(cv(argv[i])->s());
              }
            }
            m_p->filters.emplace_back(std::move(f));

          } else if (m == "fork" || m == "mux" || m == "demux" || m == "proxySOCKS4") {
            Graph::Filter f;
            f.name = m;
            f.fork = m == "fork";
            if (argc > 0 && cv(argv[0])->t() == STRING) {
              f.links.push_back(cv(argv[0])->s());
            }
            m_p->filters.emplace_back(std::move(f));

          } else if (m == "use") {
            std::string arg1, arg2;
            if (argc > 0 && cv(argv[0])->t() == STRING) arg1 = cv(argv[0])->s();
            if (argc > 1 && cv(argv[1])->t() == STRING) arg2 = cv(argv[1])->s();
            Graph::Filter f;
            f.name = m;
            f.name += ' ';
            f.name += arg1;
            f.name += " [";
            f.name += arg2;
            f.name += ']';
            m_p->filters.emplace_back(std::move(f));

          } else {
            Graph::Filter f;
            f.name = m;
            m_p->filters.emplace_back(std::move(f));
          }
        }
        ret = new ConfigValue(CONFIG_OBJECT);

      } else {
        ret = undefined();
      }
      delete fn;
      for (int i = 0; i < argc; i++) free(argv[i]);
      return ret;
    }

    virtual Value* get(Value *obj, Value *key) override {
      Value *ret = nullptr;
      if (cv(obj)->t() == CONFIG_OBJECT && cv(key)->t() == STRING) ret = new ConfigValue(CONFIG_METHOD, cv(key)->s());
      else ret = undefined();
      delete obj;
      delete key;
      return ret;
    }
  };

  //
  // Generate configuration graph from script
  //

  auto code_to_graph(Graph &g, const std::string &script) -> std::string {
    std::string error;
    int error_line, error_column;
    std::unique_ptr<pjs::Expr> ast(
      pjs::Parser::parse(script, error, error_line, error_column)
    );

    if (error.empty()) {
      ConfigReducer r(g);
      delete ast->reduce(r);
      r.flush();
    }

    return error;
  }
};

//
// Gui
//

Gui::Gui()
#ifdef PIPY_USE_GUI
  : m_www_files((const char *)s_gui_tar, sizeof(s_gui_tar))
#else
  : m_www_files(nullptr, 0)
#endif
{
}

void Gui::open(int port) {
  Log::info("[gui] Starting GUI service...");
  auto pipeline = Pipeline::make(nullptr, Pipeline::LISTEN, "GUI");
  pipeline->append(new http::RequestDecoder());
  pipeline->append(new GuiService(m_www_files));
  pipeline->append(new http::ResponseEncoder());
  auto listener = Listener::make("0.0.0.0", port);
  listener->open(pipeline);
}

} // namespace pipy