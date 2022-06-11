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

#include "graph.hpp"
#include "pipeline.hpp"
#include "filter.hpp"
#include "pjs/pjs.hpp"
#include "utils.hpp"

#include <functional>
#include <sstream>
#include <memory>

namespace pipy {

//
// Create graph from a set of pipelines
//

void Graph::from_pipelines(Graph &g, const std::set<PipelineLayout*> &pipelines) {
  for (auto *pipeline : pipelines) {
    Graph::Pipeline p;
    p.name = pipeline->name()->str();
    for (auto &f : pipeline->m_filters) {
      Graph::Filter gf;
      f->dump(gf);
      p.filters.emplace_back(std::move(gf));
    }
    switch (pipeline->type()) {
      case PipelineLayout::NAMED:
        g.add_named_pipeline(std::move(p));
        break;
      case PipelineLayout::LISTEN:
      case PipelineLayout::READ:
      case PipelineLayout::TASK:
        g.add_root_pipeline(std::move(p));
        break;
    }
  }
}

//
// Create graph from source code
//

class ConfigReducer : public pjs::Expr::Reducer {
public:
  ConfigReducer(Graph &g) : m_g(g) {}

  void flush() {
    if (m_p) {
      if (m_pt == PipelineLayout::NAMED) {
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
  PipelineLayout::Type m_pt;
  int m_pipeline_index = 0;
  int m_named_count = 0;
  int m_listen_count = 0;
  int m_task_count = 0;

  enum ConfigValueType {
    UNDEFINED,
    BOOLEAN,
    NUMBER,
    STRING,
    FUNCTION,
    CONFIG_MAKER,
    CONFIG_OBJECT,
    CONFIG_METHOD,
  };

  static const std::set<std::string> s_linking_filters;

  class ConfigValue : public pjs::Pooled<ConfigValue, pjs::Expr::Reducer::Value> {
  public:
    ConfigValue(ConfigValueType t) : m_t(t) {}
    ConfigValue(ConfigValueType t, const std::string &s) : m_t(t), m_s(s) {}
    ConfigValue(bool b) : m_t(BOOLEAN), m_b(b) {}
    ConfigValue(double n) : m_t(NUMBER), m_n(n) {}
    ConfigValue(const std::string &s) : m_t(STRING), m_s(s) {}
    ConfigValue(pjs::Expr *f) : m_t(FUNCTION), m_f(f) {}

    auto t() const -> ConfigValueType { return m_t; }
    auto b() const -> bool { return m_b; }
    auto n() const -> double { return m_n; }
    auto s() const -> const std::string& { return m_s; }
    auto f() const -> pjs::Expr* { return m_f; }

  private:
    ConfigValueType m_t;
    bool m_b = false;
    double m_n = 0;
    std::string m_s;
    pjs::Expr* m_f;
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

  virtual Value* function(pjs::Expr *expr) override {
    return new ConfigValue(expr);
  }

  virtual Value* get(const std::string &name) override {
    if (name == "pipy") return new ConfigValue(CONFIG_MAKER);
    return undefined();
  }

  virtual Value* call(Value *fn, Value **argv, int argc) override {
    Value *ret = nullptr;
    if (cv(fn)->t() == FUNCTION) {
      return cv(fn)->f()->reduce(*this);
    } if (cv(fn)->t() == CONFIG_MAKER) {
      ret = new ConfigValue(CONFIG_OBJECT);
    } else if (cv(fn)->t() == CONFIG_METHOD) {
      const auto &m = cv(fn)->s();
      if (m == "pipeline") {
        flush();
        m_pt = PipelineLayout::NAMED;
        m_p = new Graph::Pipeline;
        m_p->index = m_pipeline_index++;
        m_p->name = argc > 0 && cv(argv[0])->t() == STRING
          ? cv(argv[0])->s()
          : std::string("Pipeline #") + std::to_string(++m_named_count);

      } else if (m == "listen") {
        flush();
        m_pt = PipelineLayout::LISTEN;
        m_p = new Graph::Pipeline;
        m_p->index = m_pipeline_index++;
        m_p->name = argc > 0 && cv(argv[0])->t() == NUMBER
          ? std::string("Listen 0.0.0.0:") + std::to_string(int(cv(argv[0])->n()))
          : std::string("Listen #") + std::to_string(++m_listen_count);

      } else if (m == "task") {
        flush();
        m_pt = PipelineLayout::TASK;
        m_p = new Graph::Pipeline;
        m_p->index = m_pipeline_index++;
        m_p->name = argc > 0 && cv(argv[0])->t() == STRING
          ? std::string("Task every ") + cv(argv[0])->s()
          : std::string("Task #") + std::to_string(++m_task_count);

      } else if (m_p) {
        if (m == "link") {
          Graph::Filter f;
          f.name = m;
          for (int i = 0; i < argc; i += 2) {
            if (cv(argv[i])->t() == STRING) {
              f.subs.emplace_back();
              f.subs.back().name = cv(argv[i])->s();
            }
          }
          m_p->filters.emplace_back(std::move(f));

        } else if (m == "fork" || m == "merge" || s_linking_filters.count(m) > 0) {
          Graph::Filter f;
          f.name = m;
          if (argc > 0 && cv(argv[0])->t() == STRING) {
            f.subs.emplace_back();
            f.subs.back().name = cv(argv[0])->s();
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

const std::set<std::string> ConfigReducer::s_linking_filters{
  "mux", "demux", "muxHTTP", "demuxHTTP",
  "acceptTLS", "connectTLS",
  "acceptSOCKS",
};

//
// Graph
//

bool Graph::from_script(Graph &g, const std::string &script, std::string &error) {
  error.clear();
  int error_line, error_column;
  std::unique_ptr<pjs::Expr> ast(
    pjs::Parser::parse(script, error, error_line, error_column)
  );

  if (!error.empty()) return false;

  ConfigReducer r(g);
  delete ast->reduce(r);
  r.flush();
  return true;
}

auto Graph::to_text(std::string &error) -> std::vector<std::string> {
  std::vector<std::string> lines;

  find_roots();

  for (const auto &i : m_pipelines) {
    if (i.root) {
      std::string title("[");
      title += i.name;
      title += ']';
      lines.push_back(title);
      std::string err;
      auto node = build_tree(i, err);
      for (auto line : build_text(node)) {
        lines.push_back(std::move(line));
      }
      delete node;
      lines.push_back("");
      if (!err.empty() && error.empty()) error = err;
    }
  }

  return lines;
}

void Graph::to_json(std::string &error, std::ostream &out) {
  std::list<std::unique_ptr<Node>> roots;
  std::vector<Node*> nodes;

  find_roots();

  for (const auto &i : m_pipelines) {
    if (i.root) {
      std::string err;
      auto node = build_tree(i, err);
      roots.emplace_back(node);
      if (!err.empty() && error.empty()) error = err;
    }
  }

  std::function<void(Node*)> build_node_index;
  build_node_index = [&](Node *node) {
    node->index(nodes.size());
    nodes.push_back(node);
    for (auto child = node->children(); child; child = child->next()) {
      build_node_index(child);
    }
  };

  for (const auto &p : roots) {
    build_node_index(p.get());
  }

  out << "{\"roots\":[";

  bool first = true;
  for (const auto &p : roots) {
    if (first) first = false; else out << ',';
    out << p->index();
  }

  out << "],\"nodes\":[";

  for (size_t i = 0; i < nodes.size(); i++) {
    auto node = nodes[i];
    if (i > 0) out << ',';
    out << "{\"name\":\"";
    out << utils::escape(node->name());
    out << "\",\"t\":\"";
    switch (node->type()) {
      case Node::ERROR: out << "error"; break;
      case Node::ROOT: out << "root"; break;
      case Node::PIPELINE: out << "pipeline"; break;
      case Node::FILTER: out << "filter"; break;
      case Node::JOINT: out << "link"; break;
    }
    out << '"';
    if (node->parent()) {
      out << ",\"p\":";
      out << node->parent()->index();
    }
    if (node->children()) {
      out << ",\"c\":[";
      bool first = true;
      for (const auto *child = node->children(); child; child = child->next()) {
        if (first) first = false; else out << ',';
        out << child->index();
      }
      out << ']';
    }
    out << '}';
  }

  out << "]}";
}

void Graph::find_roots() {
  for (auto &i : m_pipelines) {
    i.root = true;
  }

  for (auto &i : m_pipelines) {
    for (auto &f : i.filters) {
      for (auto &s : f.subs) {
        if (s.index >= 0) {
          auto i = m_indexed_pipelines.find(s.index);
          if (i != m_indexed_pipelines.end()) {
            i->second->root = false;
          }
        } else {
          auto i = m_named_pipelines.find(s.name);
          if (i != m_named_pipelines.end()) {
            i->second->root = false;
          }
        }
      }
    }
  }
}

auto Graph::build_tree(const Pipeline &pipeline, std::string &error) -> Node* {
  error.clear();

  std::function<void(const Pipeline&, Node*)> build;
  build = [&](const Pipeline &pipeline, Node *pipeline_node) {
    for (const auto &f : pipeline.filters) {
      if (f.subs.empty()) {
        new Node(pipeline_node, Node::FILTER, f.name);
      } else {
        auto link_node = new Node(pipeline_node, Node::JOINT, f.out_type, f.sub_type, f.name);
        for (const auto &s : f.subs) {
          bool recursive = false;
          for (auto p = pipeline_node; p; p = p->parent()) {
            if (p->type() == Node::PIPELINE && p->name() == s.name) {
              recursive = true;
              break;
            }
          }
          if (recursive) {
            auto msg = std::string("recursive pipeline: ") + s.name;
            if (error.empty()) error = msg;
            new Node(link_node, Node::PIPELINE, msg);
            continue;
          }
          if (s.index >= 0) {
            auto i = m_indexed_pipelines.find(s.index);
            if (i == m_indexed_pipelines.end()) {
              auto msg = std::string("pipeline not found: ") + std::to_string(s.index);
              if (error.empty()) error = msg;
              new Node(link_node, Node::PIPELINE, msg);
              continue;
            }
            auto node = new Node(link_node, Node::PIPELINE, "$=>$");
            build(*i->second, node);
          } else if (s.name.empty()) {
            new Node(link_node, Node::PIPELINE, "$=>$");
          } else {
            auto i = m_named_pipelines.find(s.name);
            if (i == m_named_pipelines.end()) {
              auto msg = std::string("pipeline not found: ") + s.name;
              if (error.empty()) error = msg;
              new Node(link_node, Node::PIPELINE, msg);
              continue;
            }
            auto node = new Node(link_node, Node::PIPELINE, s.name);
            build(*i->second, node);
          }
        }
      }
    }
  };

  auto root = new Node(nullptr, Node::ROOT, pipeline.name);
  build(pipeline, root);
  return root;
}

auto Graph::build_text(Node *root) -> std::vector<std::string> {
  std::vector<std::string> lines;
  std::vector<Node*> exits;
  std::function<void(Node*, const std::string&, const std::string&, bool)> draw_node;

  auto push_line = [&](const std::string &line, Node *exit = nullptr) {
    std::string trim(line);
    while (trim.back() == ' ') trim.pop_back();
    if (lines.empty() || exit || trim != lines.back()) {
      lines.push_back(trim);
      exits.push_back(exit);
    }
  };

  auto find_output = [](Node *node) -> Node* {
    if (node->next()) return node->next();
    Node *p = node->parent();
    while (p) {
      if (p->type() == Node::JOINT) {
        if (p->out_type() != Filter::Dump::OUTPUT_FROM_SUBS) return nullptr;
        if (p->next()) return p;
      } else if (p->type() == Node::ROOT) {
        return p;
      }
      p = p->parent();
    }
    return nullptr;
  };

  auto draw_output = [&](Node *node, const std::string &base, bool parallel) {
    auto first = lines.size();
    for (auto i = lines.size() - 1; i > 0; i--) {
      if (exits[i] == node) first = i;
    }
    int max_width = base.length();
    for (auto i = first; i < lines.size(); i++) {
      int width = lines[i].length();
      if (width > max_width) max_width = width;
    }
    for (auto i = first; i < lines.size(); i++) {
      auto &line = lines[i];
      int padding = max_width - line.length();
      if (exits[i] == node) {
        if (line.back() == '|') {
          line += std::string(padding + 3, parallel ? '=' : '-');
        } else {
          line += ' ';
          line += std::string(padding + 2, parallel ? '=' : '-');
        }
        line += '>';
      } else {
        line += std::string(padding + 4, ' ');
      }
      line += parallel ? "||" : "|";
    }
    auto padding_width = max_width - base.length();
    auto padding = std::string(padding_width + 1, parallel ? '=' : '-');
    if (node->type() == Node::ROOT) {
      push_line(base + '<' + padding + (parallel ? "==||" : "--|"));
    } else {
      push_line(base + (parallel ? "||" : " |") + '<' + padding + (parallel ? "||" : "|"));
      push_line(base + (parallel ? "||" : " |"));
      push_line(base + (parallel ? "vv" : " v"));
    }
  };

  draw_node = [&](
    Node *node,
    const std::string &base_pipeline,
    const std::string &base_filter,
    bool parallel
  ) {
    switch (node->type()) {
      case Node::ERROR: {
        std::stringstream ss;
        ss << base_filter << "!!ERROR: " << node->name();
        push_line(ss.str());
        break;
      }
      case Node::ROOT: {
        push_line("----->|");
        push_line("      |");
        auto base = base_pipeline + "     ";
        for (Node *child = node->children(); child; child = child->next()) {
          draw_node(child, base, base, parallel);
        }
        push_line(base_filter);
        draw_output(node, base_filter, false);
        break;
      }
      case Node::PIPELINE: {
        std::stringstream ss;
        ss << base_pipeline << '[' << node->name() << ']';
        push_line(ss.str(), node->children() ? nullptr : find_output(node));
        for (Node *child = node->children(); child; child = child->next()) {
          draw_node(child, base_filter, base_filter, parallel);
        }
        push_line(base_filter);
        break;
      }
      case Node::FILTER: {
        std::stringstream ss;
        ss << base_filter << node->name();
        auto *out = find_output(node);
        if (out == node->next()) out = nullptr;
        push_line(ss.str(), out);
        if (node->name() == "output") {
          push_line(base_filter + (parallel ? "||"        : " |"       ));
          push_line(base_filter + (parallel ? "||==> ..." : " |--> ..."));
        }
        break;
      }
      case Node::JOINT: {
        std::stringstream ss;
        ss << base_filter << node->name();
        push_line(ss.str());
        if (node->sub_type() == Filter::Dump::DEMUX) parallel = true;
        auto *out = find_output(node);
        auto base = base_filter + (parallel ? "||" : " |");
        auto head = base + (parallel ? "==> " : "--> ");
        auto tail = std::string(base_filter);
        if (node->out_type() == Filter::Dump::OUTPUT_FROM_OTHERS) {
          tail += (parallel ? ".." : " .");
        } else if (node->out_type() == Filter::Dump::OUTPUT_FROM_SELF) {
          tail += out ? (parallel ? "||" : " |") : "  ";
        } else {
          tail += "  ";
        }
        push_line(base);
        base += "     ";
        tail += "     ";
        auto sub_parallel = parallel;
        if (node->sub_type() == Filter::Dump::MUX) sub_parallel = false;
        for (Node *child = node->children(); child; child = child->next()) {
          if (child->next()) {
            draw_node(child, head, base, sub_parallel);
          } else {
            draw_node(child, head, tail, sub_parallel);
          }
        }
        switch (node->out_type()) {
          case Filter::Dump::OUTPUT_FROM_OTHERS: {
            push_line(tail);
            push_line(base_filter + (parallel ? "||<== ..." : " |<-- ..."));
            push_line(base_filter + (parallel ? "||"        : " |"       ));
            if (node->next()) {
              push_line(base_filter + (parallel ? "vv" : " v"));
            } else {
              push_line(base_filter + (parallel ? "||" : " |"));
            }
            break;
          }
          case Filter::Dump::OUTPUT_FROM_SELF: {
            if (node->next()) {
              push_line(base_filter + (parallel ? "vv" : " v"));
            } else if (out) {
              push_line(tail);
              push_line(base_filter + (parallel ? "||" : " |"), out);
            }
            break;
          }
          case Filter::Dump::OUTPUT_FROM_SUBS: {
            if (out && out == node->next()) {
              push_line(tail);
              draw_output(node, base_filter, parallel);
            }
            break;
          }
          default: break;
        }
        break;
      }
    }
  };

  draw_node(root, "", "", false);
  return lines;
}

//
// Graph::Node
//

Graph::Node::~Node() {
  while (auto *node = m_children.head()) {
    m_children.remove(node);
    delete node;
  }
}

} // namespace pipy
