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

#ifndef GRAPH_HPP
#define GRAPH_HPP

#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace pipy {

class Filter;
class Pipeline;

//
// Graph
//

class Graph {
public:
  struct Filter {
    std::string name;
    std::list<std::string> links;
    bool fork = false;
    int row = 0;
    int column = 0;
  };

  struct Pipeline {
    std::string name;
    std::list<Filter> filters;
    bool root = false;
  };

  static void from_pipelines(Graph &g, const std::set<pipy::Pipeline*> &pipelines);
  static bool from_script(Graph &g, const std::string &script, std::string &error);

  void add_root_pipeline(Pipeline &&p) {
    m_root_pipelines[p.name] = std::move(p);
  }

  void add_named_pipeline(Pipeline &&p) {
    m_named_pipelines[p.name] = std::move(p);
  }

  auto to_text(std::string &error) -> std::vector<std::string>;
  void to_json(std::string &error, std::ostream &out);

private:
  class Node {
  public:
    enum Type {
      ERROR,
      ROOT,
      PIPELINE,
      FILTER,
      LINK,
      FORK,
    };

    Node(Node *parent, Type type, const std::string &name)
      : m_parent(parent)
      , m_type(type)
      , m_name(name)
    {
      if (parent) {
        parent->m_children.push_back(this);
      }
    }

    ~Node() {
      for (auto child : m_children) {
        delete child;
      }
    }

    auto type() const -> Type { return m_type; }
    auto name() const -> const std::string& { return m_name; }
    auto parent() const -> Node* { return m_parent; }
    auto children() const -> const std::list<Node*>& { return m_children; }
    auto index() const -> int { return m_index; }
    void index(int i) { m_index = i; }

  private:
    Node* m_parent;
    Type m_type;
    std::string m_name;
    std::list<Node*> m_children;
    int m_index = 0;
  };

  std::map<std::string, Pipeline> m_root_pipelines;
  std::map<std::string, Pipeline> m_named_pipelines;

  void find_roots();
  auto build_tree(const Pipeline &pipeline, std::string &error) -> Node*;
  auto build_text(Node *root) -> std::vector<std::string>;
  auto build_json(Node *root) -> std::string;
};

} // namespace pipy

#endif // GRAPH_HPP