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
#include "utils.hpp"

#include <functional>
#include <sstream>
#include <memory>

namespace pipy {

auto Graph::to_text(std::string &error) -> std::vector<std::string> {
  std::vector<std::string> lines;

  find_roots();

  auto build_one = [&](const Pipeline &pipeline) {
    std::string title("[");
    title += pipeline.name;
    title += ']';
    lines.push_back(title);
    std::string err;
    auto node = build_tree(pipeline, err);
    for (auto line : build_text(node)) {
      lines.push_back(std::move(line));
    }
    delete node;
    lines.push_back("");
    if (!err.empty() && error.empty()) error = err;
  };

  for (const auto &i : m_root_pipelines) {
    build_one(i.second);
  }

  for (const auto &i : m_named_pipelines) {
    if (i.second.root) {
      build_one(i.second);
    }
  }

  return lines;
}

void Graph::to_json(std::string &error, std::ostream &out) {
  std::list<std::unique_ptr<Node>> roots;
  std::vector<Node*> nodes;

  find_roots();

  auto build_one = [&](const Pipeline &pipeline) {
    std::string err;
    auto node = build_tree(pipeline, err);
    roots.emplace_back(node);
    if (!err.empty() && error.empty()) error = err;
  };

  for (const auto &i : m_root_pipelines) {
    build_one(i.second);
  }

  for (const auto &i : m_named_pipelines) {
    if (i.second.root) {
      build_one(i.second);
    }
  }

  std::function<void(Node*)> build_node_index;
  build_node_index = [&](Node *node) {
    node->index(nodes.size());
    nodes.push_back(node);
    for (auto child : node->children()) {
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
      case Node::FORK: out << "fork"; break;
      case Node::LINK: out << "link"; break;
    }
    out << '"';
    if (node->parent()) {
      out << ",\"p\":";
      out << node->parent()->index();
    }
    if (!node->children().empty()) {
      out << ",\"c\":[";
      bool first = true;
      for (const auto child : node->children()) {
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
  for (auto &i : m_named_pipelines) {
    i.second.root = true;
  }

  auto find_links = [this](const Pipeline &pipeline) {
    for (auto &f : pipeline.filters) {
      for (auto &l : f.links) {
        auto i = m_named_pipelines.find(l);
        if (i != m_named_pipelines.end()) {
          i->second.root = false;
        }
      }
    }
  };

  for (auto &i : m_root_pipelines) find_links(i.second);
  for (auto &i : m_named_pipelines) find_links(i.second);
}

auto Graph::build_tree(const Pipeline &pipeline, std::string &error) -> Node* {
  error.clear();

  std::function<void(const Pipeline&, Node*)> build;
  build = [&](const Pipeline &pipeline, Node *pipeline_node) {
    for (const auto &f : pipeline.filters) {
      if (f.links.empty()) {
        new Node(pipeline_node, Node::FILTER, f.name);
      } else {
        auto link_node = new Node(pipeline_node, f.fork ? Node::FORK : Node::LINK, f.name);
        for (const auto &l : f.links) {
          bool recursive = false;
          for (auto p = pipeline_node; p; p = p->parent()) {
            if (p->type() == Node::PIPELINE && p->name() == l) {
              recursive = true;
              break;
            }
          }
          if (recursive) {
            auto msg = std::string("recursive pipeline: ") + l;
            if (error.empty()) error = msg;
            new Node(link_node, Node::PIPELINE, msg);
            continue;
          }
          auto i = m_named_pipelines.find(l);
          if (i == m_named_pipelines.end()) {
            auto msg = std::string("pipeline not found: ") + l;
            if (error.empty()) error = msg;
            new Node(link_node, Node::PIPELINE, msg);
            continue;
          }
          auto node = new Node(link_node, Node::PIPELINE, l);
          build(i->second, node);
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
  std::function<void(Node*)> draw_node;

  draw_node = [&](Node *node) {
    std::string base;

    Node *parent = nullptr;
    if (node->type() == Node::PIPELINE) {
      parent = node->parent()->parent();
      base = "  |--> ";
    } else {
      parent = node->parent();
    }

    for (auto p = parent; p && p->type() != Node::ROOT; p = p->parent()->parent()) {
      if (p->parent()->type() != Node::FORK && p == p->parent()->children().back()) {
        base = std::string(7, ' ') + base;
      } else {
        base = std::string("  |    ") + base;
      }
    }

    if (node->type() == Node::ROOT) {
      lines.push_back("----->|");
      exits.push_back(nullptr);
    }

    base = std::string(4, ' ') + base;

    std::string line(base);
    if (node->type() == Node::ROOT) {
      line += "  |";
    } else if (node->type() == Node::PIPELINE) {
      line += '[';
      line += node->name();
      line += ']';
    } else {
      line += ' ';
      line += node->name();
    }

    Node *exit = nullptr;

    if ((node->type() == Node::PIPELINE && node->children().empty()) ||
        (node->type() == Node::FILTER && node->parent()->children().back() == node)
    ) {
      auto pipe = node->type() == Node::FILTER ? node->parent() : node;
      exit = pipe->parent();
      if (exit) {
        while (
          exit->type() == Node::LINK &&
          exit->parent()->type() != Node::ROOT &&
          exit == exit->parent()->children().back()
        ) {
          pipe = exit->parent();
          if (pipe->type() == Node::ROOT) {
            exit = nullptr;
            break;
          } else {
            exit = pipe->parent();
          }
        }
      }
      if (exit && exit->type() != Node::LINK) exit = nullptr;
    }

    lines.push_back(line);
    exits.push_back(exit);

    for (auto c : node->children()) {
      if (node->type() == Node::LINK || node->type() == Node::FORK) {
        lines.push_back(base + "  |");
        exits.push_back(nullptr);
      }
      draw_node(c);
    }

    if (node->type() == Node::LINK) {
      if (node->parent()->type() == Node::ROOT || node != node->parent()->children().back()) {
        int end = exits.size() - 1;
        int start = end;
        for (int i = start; i >= 0; i--) {
          if (exits[i] == node) {
            start = i;
          }
        }
        int max_length = 0;
        for (int i = start; i <= end; i++) {
          max_length = std::max(max_length, int(lines[i].length()));
        }
        for (int i = start; i <= end; i++) {
          int padding = max_length - lines[i].length();
          if (exits[i] == node) {
            lines[i] += ' ';
            lines[i] += std::string(padding, '-');
            lines[i] += "-->|";
          } else {
            lines[i] += std::string(padding + 4, ' ');
            lines[i] += '|';
          }
        }
        lines.push_back(base + std::string(max_length - base.length() + 4, ' ') + '|');
        exits.push_back(nullptr);
        if (node->parent()->type() == Node::ROOT && node == node->parent()->children().back()) {
          std::string line("<");
          line += std::string(max_length + 3, '-');
          line += '|';
          lines.push_back(line);
          exits.push_back(nullptr);
        } else {
          lines.push_back(base + std::string(max_length - base.length() + 4, ' ') + 'v');
          lines.push_back(base + "  |<" + std::string(max_length - base.length() + 1, '-'));
          lines.push_back(base + "  |");
          lines.push_back(base + "  v");
          exits.push_back(nullptr);
          exits.push_back(nullptr);
          exits.push_back(nullptr);
          exits.push_back(nullptr);
        }
      }

    } else if (node->type() == Node::FORK) {
      lines.push_back(base + "  |");
      exits.push_back(nullptr);

    } else if (node->type() == Node::ROOT) {
      if (node->children().empty() || node->children().back()->type() != Node::LINK) {
        lines.push_back(std::string(6, ' ') + '|');
        lines.push_back("<-----|");
      }
    }
  };

  draw_node(root);
  return lines;
}

} // namespace pipy
