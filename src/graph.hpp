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

#include "filter.hpp"
#include "list.hpp"

#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace pipy {

class Worker;

//
// Graph
//

class Graph {
public:
  struct Link {
    int index;
    std::string name;
  };

  struct Filter : public pipy::Filter::Dump {
    int row = 0;
    int column = 0;
  };

  struct Pipeline {
    int index = -1;
    std::string name;
    std::string label;
    std::list<Filter> filters;
    bool root = false;
  };

  static void from_pipelines(Graph &g, const std::set<PipelineLayout*> &pipelines);
  static bool from_script(Graph &g, const std::string &script, std::string &error);

  auto add_pipeline(Pipeline &&p) -> int;

  auto to_text(std::string &error) -> std::vector<std::string>;
  void to_json(std::string &error, std::ostream &out);

private:
  class Node : public List<Node>::Item {
  public:
    enum Type {
      ERROR,
      ROOT,
      PIPELINE,
      FILTER,
      JOINT,
    };

    enum LinkType {
      NO_LINKS,
      BRANCH,
      FORK,
      DEMUX,
      MUX,
      MUX_FORK,
    };

    Node(Node *parent, Type type, const std::string &name, int pipeline_index = -1)
      : Node(parent, type, Filter::Dump::OUTPUT_FROM_SELF, Filter::Dump::NO_SUBS, name, pipeline_index) {}

    Node(
      Node *parent, Type type,
      Filter::Dump::OutType out_type,
      Filter::Dump::SubType sub_type,
      const std::string &name,
      int pipeline_index = -1
    ) : m_parent(parent)
      , m_name(name)
      , m_type(type)
      , m_out_type(out_type)
      , m_sub_type(sub_type)
      , m_pipeline_index(pipeline_index)
    {
      if (parent) {
        parent->m_children.push(this);
      }
    }

    ~Node();

    auto type() const -> Type { return m_type; }
    auto out_type() const -> Filter::Dump::OutType { return m_out_type; }
    auto sub_type() const -> Filter::Dump::SubType { return m_sub_type; }
    auto name() const -> const std::string& { return m_name; }
    auto parent() const -> Node* { return m_parent; }
    auto children() const -> Node* { return m_children.head(); }
    auto index() const -> int { return m_index; }
    void index(int i) { m_index = i; }
    auto pipeline_index() const -> int { return m_pipeline_index; }

  private:
    Node* m_parent;
    List<Node> m_children;
    std::string m_name;
    Type m_type;
    Filter::Dump::OutType m_out_type;
    Filter::Dump::SubType m_sub_type;
    int m_index = 0;
    int m_pipeline_index;
  };

  std::list<Pipeline> m_pipelines;
  std::map<int, Pipeline*> m_indexed_pipelines;
  std::map<std::string, Pipeline*> m_root_pipelines;
  std::map<std::string, Pipeline*> m_named_pipelines;
  int m_pipeline_index = 0;

  void find_roots();
  auto build_tree(const Pipeline &pipeline, std::string &error) -> Node*;
  auto build_text(Node *root) -> std::vector<std::string>;
  auto build_json(Node *root) -> std::string;
};

} // namespace pipy

#endif // GRAPH_HPP
