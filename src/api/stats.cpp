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

#include "stats.hpp"
#include "worker.hpp"
#include "utils.hpp"
#include "log.hpp"

#include <cmath>

//
// Initial state:
//   {
//     "k": "metric-1",
//     "l": "label-1/label-2",
//     "t": "Counter",
//     "v": 123,
//     "s": [
//       {
//         "k": "label-value-1",
//         "v": 123,
//         "s": [...]
//       }
//     ]
//   }
//
// Update state:
//   {
//     "v": 123,
//     "s": [
//       {
//         "v": 123,
//         "s": [...]
//       },
//       123
//     ]
//   }
//
// Vector:
//   {
//     "k": "latency-1",
//     "t": "Histogram[1,2,4,8,16,32]",
//     "v": [12345, 1234, 123, 12, 1, 0]
//   }
//

namespace pipy {
namespace stats {

static std::string s_prefix_histogram("Histogram[");
thread_local static pjs::ConstStr s_str_Counter("Counter");
thread_local static pjs::ConstStr s_str_Gauge("Gauge");
thread_local static pjs::ConstStr s_str_count("count");
thread_local static pjs::ConstStr s_str_sum("sum");
thread_local static Data::Producer s_dp("Stats");

//
// Metric
//

auto Metric::local() -> MetricSet& {
  thread_local static MetricSet s_local_metric_set;
  return s_local_metric_set;
}

Metric::Metric(pjs::Str *name, pjs::Array *label_names, MetricSet *set)
  : m_root(nullptr)
  , m_name(name)
  , m_label_index(-1)
  , m_label_names(std::make_shared<std::vector<pjs::Ref<pjs::Str>>>())
{
  if (label_names) {
    std::string shape;
    auto n = label_names->length();
    m_label_names->resize(n);
    for (auto i = 0; i < n; i++) {
      pjs::Value v; label_names->get(i, v);
      auto *s = v.to_string();
      if (!shape.empty()) shape += '/';
      shape += s->str();
      m_label_names->at(i) = s;
      s->release();
    }
    m_shape = pjs::Str::make(std::move(shape));
  } else {
    m_shape = pjs::Str::empty;
  }

  if (set) {
    set->add(this);
  } else {
    local().add(this);
  }
}

Metric::Metric(Metric *parent, pjs::Str **labels)
  : m_root(parent->m_root)
  , m_name(parent->m_name)
  , m_label(labels[parent->m_label_index + 1])
  , m_label_index(parent->m_label_index + 1)
  , m_label_names(parent->m_label_names)
{
  parent->m_subs.emplace_back();
  parent->m_subs.back() = this;
  parent->m_sub_map[m_label] = this;
}

auto Metric::with_labels(pjs::Str *const *labels, int count) -> Metric* {
  int num_labels = m_label_names->size();
  if (m_label_index + 1 >= num_labels) {
    return nullptr;
  }

  auto s = m_label_index + 1;
  auto n = std::min(s + count, num_labels);

  pjs::Str *l[n];
  for (int i = s; i < n; i++) {
    l[i] = labels[i - s];
  }

  Metric *metric = this;
  for (int i = s; i < n; i++) {
    metric = metric->get_sub(l);
  }

  return metric;
}

auto Metric::type() -> pjs::Str* {
  auto *m = m_root ? m_root : this;
  if (m->m_type) return m_type;
  m_type = m->get_type();
  return m_type;
}

void Metric::clear() {
  for (const auto &i : m_subs) {
    i->clear();
  }
  m_subs.clear();
  m_sub_map.clear();
  m_has_value = false;
}

void Metric::create_value() {
  m_has_value = true;
}

void Metric::zero_all() {
  zero();
  for (const auto &i : m_subs) {
    i->zero_all();
  }
}

auto Metric::get_sub(pjs::Str **labels) -> Metric* {
  auto k = labels[m_label_index + 1];
  auto i = m_sub_map.find(k);
  if (i != m_sub_map.end()) return i->second;
  return create_new(this, labels);
}

auto Metric::get_sub(int i) -> Metric* {
  if (0 <= i && i < m_subs.size()) {
    return m_subs[i].get();
  } else {
    return nullptr;
  }
}

//
// MetricSet
//

auto MetricSet::get(pjs::Str *name) -> Metric* {
  auto i = m_metric_map.find(name);
  if (i == m_metric_map.end()) return nullptr;
  return m_metrics[i->second];
}

auto MetricSet::get(int i) -> Metric* {
  if (0 <= i && i < m_metrics.size()) {
    return m_metrics[i].get();
  } else {
    return nullptr;
  }
}

void MetricSet::add(Metric *metric) {
  auto i = m_metric_map.find(metric->name());
  if (i == m_metric_map.end()) {
    m_metric_map[metric->name()] = m_metrics.size();
    m_metrics.emplace_back();
    m_metrics.back() = metric;
  } else {
    m_metrics[i->second] = metric;
  }
}

void MetricSet::collect_all() {
  for (const auto &m : m_metrics) {
    m->collect();
  }
}

void MetricSet::clear() {
  m_metric_map.clear();
  m_metrics.clear();
}

//
// Prometheus
//

template<class Node>
class Prometheus {
public:
  Prometheus(
    const std::string &name,
    const std::string &extra_labels,
    const std::vector<std::string> &label_names,
    pjs::Str::CharData *label_values[],
    const char *le_str,
    const std::function<void(const void *, size_t)> &out
  ) : m_name(name)
    , m_extra_labels(extra_labels)
    , m_label_names(label_names)
    , m_label_values(label_values)
    , m_le_str(le_str)
    , m_out(out) {}

  void output(Node *node, int level) {
    static const std::string s_bucket("_bucket");
    static const std::string s_sum("_sum");
    static const std::string s_count("_count");

    if (level > m_label_names.size()) return;

    if (level > 0) {
      m_label_values[level-1] = node->get_key();
    }

    if (node->has_value) {
      if (m_le_str) {
        auto le = 0;
        auto *p = m_le_str;
        while (p) {
          auto q = p;
          while (*q && *q != ',' && *q != ']') q++;
          auto n = q - p;
          if (*p == '"') {
            p++; n--;
            if (n > 1 && *(q-1) == '"') n--;
          }
          output(m_name);
          output(s_bucket);
          output(level, node->values[le++], p, n);
          p = (*q == ',' ? q+1 : nullptr);
        }
        output(m_name);
        output(s_count);
        output(level, node->values[le++]);
        output(m_name);
        output(s_sum);
        output(level, node->values[le++]);

      } else {
        output(m_name);
        output(level, node->values[0]);
      }
    }

    node->for_subs([=](Node *sub) {
      output(sub, level + 1);
    });
  }

private:
  const std::string &m_name;
  const std::string &m_extra_labels;
  const std::vector<std::string> &m_label_names;
  pjs::Str::CharData **m_label_values;
  const char *m_le_str;
  const std::function<void(const void *, size_t)> &m_out;

  void output(char c) { m_out(&c, 1); }
  void output(const std::string &s) { m_out(s.c_str(), s.length()); }
  void output(const void *data, size_t size) { m_out(data, size); }

  void output(
    int level, double num,
    const char *le = nullptr, int le_len = 0
  ) {
    static const std::string s_le("le=");
    if (level > 0 || !m_extra_labels.empty() || le) {
      bool first = true;
      output('{');
      if (!m_extra_labels.empty()) {
        output(m_extra_labels);
        first = false;
      }
      for (int i = 0, n = (le ? level+1 : level); i < n; i++) {
        if (first) first = false; else output(',');
        if (i == level) {
          output(s_le);
          output('"');
          output(le, le_len);
          output('"');
        } else {
          output(m_label_names[i]);
          output('=');
          output('"');
          output(m_label_values[i]->str());
          output('"');
        }
      }
      output('}');
    }
    output(' ');
    char buf[100];
    auto len = pjs::Number::to_string(buf, sizeof(buf), num);
    output(buf, len);
    output('\n');
  }
};

//
// MetricData
//

MetricData::~MetricData() {
  auto *p = m_entries;
  while (p) {
    auto *ent = p; p = p->next;
    delete ent;
  }
}

void MetricData::update(MetricSet &metrics) {
  std::function<void(int, Node*, Metric*)> update;

  update = [&](int level, Node *node, Metric *metric) {
    if (level > 0) node->key = metric->label()->data();

    auto dim = metric->dimensions();
    node->has_value = metric->has_value();
    for (int d = 0; d < dim; d++) {
      node->values[d] = metric->get_value(d);
    }

    auto **sub = &node->subs;
    for (const auto &m : metric->m_subs) {
      auto s = *sub;
      if (!s) s = *sub = Node::make(dim);
      update(level + 1, s, m);
      sub = &s->next;
    }

    auto s = *sub; *sub = nullptr;
    while (s) {
      auto sub = s; s = s->next;
      delete sub;
    }
  };

  auto **ent = &m_entries;
  for (const auto &i : metrics.m_metrics) {
    auto metric = i.get();
    auto e = *ent;
    if (!e ||
      e->name != metric->name()->data() ||
      e->type != metric->type()->data() ||
      e->shape != metric->shape()->data() ||
      e->dimensions != metric->dimensions()
    ) {
      if (!e) e = *ent = new Entry;
      e->root.reset(Node::make(metric->dimensions()));
      e->name = metric->name()->data();
      e->type = metric->type()->data();
      e->shape = metric->shape()->data();
      e->dimensions = metric->dimensions();
      e->labels.clear();
    }
    update(0, e->root.get(), metric);
    ent = &e->next;
  }

  auto e = *ent; *ent = nullptr;
  while (e) {
    auto ent = e; e = e->next;
    delete ent;
  }
}

void MetricData::deserialize(const Data &in) {
  Deserializer des(this);
  if (!JSON::visit(in, &des)) {
    Log::error("[stats] JSON deserialization failed for metrics");
    return;
  }
  if (des.has_error()) {
    Log::error("[stats] Invalid JSON structure for metrics");
  }
}

void MetricData::to_prometheus(const std::string &extra_labels, const std::function<void(const void *, size_t)> &out) const {
  static const std::string s_prefix_TYPE("# TYPE ");
  static const std::string s_type_counter(" counter\n");
  static const std::string s_type_gauge(" gauge\n");
  static const std::string s_type_histogram(" histogram\n");
  auto print = [&](const std::string &str) { out(str.c_str(), str.length()); };
  for (auto *ent = m_entries; ent; ent = ent->next) {
    if (auto root = ent->root.get()) {
      print(s_prefix_TYPE);
      print(ent->name->str());
      const char *le_str = nullptr;
      auto *name = ent->name.get();
      auto *type = ent->type.get();
      auto *shape = ent->shape.get();
      if (utils::starts_with(type->str(), s_prefix_histogram)) {
        le_str = type->c_str() + s_prefix_histogram.length();
        print(s_type_histogram);
      } else if (ent->type->str() == "Gauge") {
        print(s_type_gauge);
      } else {
        print(s_type_counter);
      }
      if (shape->size() > 0 && ent->labels.empty()) {
        auto labels = utils::split(shape->str(), '/');
        ent->labels.resize(labels.size());
        int i = 0;
        for (auto &s : labels) { ent->labels[i++] = std::move(s); }
      }
      pjs::Str::CharData *label_values[ent->labels.size()];
      Prometheus<Node> prom(name->str(), extra_labels, ent->labels, label_values, le_str, out);
      prom.output(root, 0);
    }
  }
}

//
// MetricData::Node
//

auto MetricData::Node::make(int dimensions) -> Node* {
  auto len = sizeof(Node) + (dimensions - 1) * sizeof(double);
  auto ptr = (Node *)std::calloc(len, 1);
  new (ptr) Node;
  return ptr;
}

MetricData::Node::~Node() {
  auto *s = subs;
  while (s) {
    auto sub = s; s = s->next;
    delete sub;
  }
}

//
// MetricData::Deserializer
//

MetricData::Deserializer::~Deserializer() {
  while (m_current_level) {
    pop();
  }
}

void MetricData::Deserializer::push(Level *level) {
  level->parent = m_current_level;
  m_current_level = level;
}

void MetricData::Deserializer::pop() {
  if (auto *level = m_current_level) {
    m_current_level = level->parent;
    delete level;
  }
}

void MetricData::Deserializer::error() {
  m_has_error = true;
}

void MetricData::Deserializer::null() {
  error();
}

void MetricData::Deserializer::boolean(bool b) {
  error();
}

void MetricData::Deserializer::integer(int64_t i) {
  number(i);
}

void MetricData::Deserializer::number(double n) {
  if (!m_has_error) {
    if (auto level = m_current_level) {
      switch (level->kind) {
        case Level::Kind::ENTRIES: {
          if (auto ent = m_entries.next()) {
            m_current_entry = ent;
            if (auto node = ent->root.get()) {
              node->values[0] = n;
              return;
            }
          }
          break;
        }
        case Level::Kind::SUBS: {
          if (auto sub = level->subs.next()) {
            sub->values[0] = n;
            return;
          }
          break;
        }
        case Level::Kind::METRIC: {
          if (auto *node = level->node) {
            if (level->field == Level::Field::VALUE) {
              node->values[0] = n;
              return;
            }
          }
          break;
        }
        case Level::Kind::VALUES: {
          int i = level->index++;
          if (i < m_current_entry->dimensions) {
            level->node->values[i] = n;
            return;
          }
          break;
        }
        default: break;
      }
    }
    error();
  }
}

void MetricData::Deserializer::string(const char *s, size_t len) {
  if (!m_has_error) {
    if (auto level = m_current_level) {
      if (level->kind == Level::Kind::METRIC) {
        auto is_entry = (level->parent && level->parent->kind == Level::Kind::ENTRIES);
        pjs::Ref<pjs::Str> str(pjs::Str::make(s, len));
        switch (level->field) {
          case Level::Field::KEY:
            if (is_entry) {
              m_current_entry->name = str->data();
            } else {
              level->node->key = str->data();
            }
            return;
          case Level::Field::TYPE:
            if (is_entry) {
              int dim = 1;
              if (utils::starts_with(str->str(), s_prefix_histogram)) {
                for (auto c : str->str()) if (c == ',') dim++;
                dim += 2;
              }
              if (dim <= 100) {
                auto node = Node::make(dim);
                m_current_entry->type = str->data();
                m_current_entry->dimensions = dim;
                m_current_entry->root.reset(node);
                level->node = node;
                return;
              }
            }
            break;
          case Level::Field::LABELS:
            if (is_entry) {
              m_current_entry->shape = str->data();
              return;
            }
            break;
          default: break;
        }
      }
    }
    error();
  }
}

void MetricData::Deserializer::map_start() {
  if (!m_has_error) {
    if (auto *level = m_current_level) {
      switch (level->kind) {
        case Level::Kind::ENTRIES: {
          m_current_entry = m_entries.next([]() { return new Entry; });
          push(new Level(Level::Kind::METRIC, m_current_entry->root.get()));
          break;
        }
        case Level::Kind::SUBS: {
          auto sub = level->subs.next([this]() { return Node::make(m_current_entry->dimensions); });
          push(new Level(Level::Kind::METRIC, sub));
          break;
        }
        default: error(); break;
      }
    } else {
      push(new Level(Level::Kind::ROOT));
    }
  }
}

void MetricData::Deserializer::map_key(const char *s, size_t len) {
  if (!m_has_error) {
    if (auto *level = m_current_level) {
      switch (level->kind) {
        case Level::Kind::ROOT:
          if (!std::strncmp(s, "metrics", len)) { level->field = Level::Field::METRICS; return; }
          break;
        case Level::Kind::METRIC:
          if (len == 1) {
            switch (*s) {
              case 'k': level->field = Level::Field::KEY; return;
              case 't': level->field = Level::Field::TYPE; return;
              case 'l': level->field = Level::Field::LABELS; return;
              case 'v': level->field = Level::Field::VALUE; return;
              case 's': level->field = Level::Field::SUB; return;
              default: break;
            }
          }
          break;
        default: break;
      }
    }
    error();
  }
}

void MetricData::Deserializer::map_end() {
  if (!m_has_error) {
    pop();
  }
}

void MetricData::Deserializer::array_start() {
  if (!m_has_error) {
    if (auto level = m_current_level) {
      switch (level->kind) {
        case Level::Kind::ROOT: {
          if (level->field == Level::Field::METRICS) {
            push(new Level(Level::Kind::ENTRIES));
            return;
          }
          break;
        }
        case Level::Kind::ENTRIES: {
          if (auto ent = m_entries.next()) {
            m_current_entry = ent;
            if (auto node = ent->root.get()) {
              push(new Level(Level::Kind::VALUES, node));
              return;
            }
          }
          break;
        }
        case Level::Kind::SUBS: {
          if (auto sub = level->subs.next()) {
            push(new Level(Level::Kind::VALUES, sub));
            return;
          }
          break;
        }
        case Level::Kind::METRIC: {
          if (auto *node = level->node) {
            if (level->field == Level::Field::VALUE) {
              push(new Level(Level::Kind::VALUES, node));
              return;
            } else if (level->field == Level::Field::SUB) {
              push(new Level(Level::Kind::SUBS, node, &node->subs));
              return;
            }
          }
          break;
        }
        default: break;
      }
    }
    error();
  }
}

void MetricData::Deserializer::array_end() {
  if (!m_has_error) {
    pop();
  }
}

//
// MetricDataSum
//

MetricDataSum::~MetricDataSum() {
  auto *e = m_entries.head();
  while (e) {
    auto *ent = e; e = e->next();
    delete ent;
  }
}

void MetricDataSum::sum(MetricData &data, bool initial) {
  std::function<void(int, Node*, MetricData::Node*)> sum;

  sum = [&](int dimensions, Node *node, MetricData::Node* src_node) {
    node->has_value |= src_node->has_value;
    for (int i = 0; i < dimensions; i++) {
      node->values[i] += src_node->values[i];
    }

    auto &submap = node->submap;
    for (auto s = src_node->subs; s; s = s->next) {
      auto *key = pjs::Str::make(s->key)->retain();
      Node *sub = nullptr;
      auto i = submap.find(key);
      if (i == submap.end()) {
        submap[key] = sub = Node::make(dimensions);
        sub->key = key;
        node->subs.push(sub);
      } else {
        sub = i->second;
      }
      key->release();
      sum(dimensions, sub, s);
    }
  };

  for (auto *e = data.m_entries; e; e = e->next) {
    auto *name = pjs::Str::make(e->name)->retain();
    auto *type = pjs::Str::make(e->type)->retain();
    auto *shape = pjs::Str::make(e->shape)->retain();

    auto &ent = m_entry_map[name];
    if (!ent || (initial && (
      ent->type != type ||
      ent->shape != shape ||
      ent->dimensions != e->dimensions
    ))) {
      if (!ent) {
        ent = new Entry;
        m_entries.push(ent);
      }
      ent->name = name;
      ent->type = type;
      ent->shape = shape;
      ent->dimensions = e->dimensions;
      ent->labels.clear();
      ent->root.reset(Node::make(ent->dimensions));
    }

    name->release();
    type->release();
    shape->release();

    if (initial) {
      ent->root->zero(e->dimensions);
    }

    sum(
      std::min(ent->dimensions, e->dimensions),
      ent->root.get(), e->root.get()
    );
  }
}

void MetricDataSum::serialize(Data &out, bool initial) {
  Data::Builder db(out, &s_dp);
  serialize(db, initial);
  db.flush();
}

void MetricDataSum::serialize(Data::Builder &db, bool initial) {
  static const std::string s_metrics("\"metrics\":"); // metrics
  static const std::string s_k("\"k\":"); // key
  static const std::string s_t("\"t\":"); // type
  static const std::string s_v("\"v\":"); // value
  static const std::string s_l("\"l\":"); // label
  static const std::string s_s("\"s\":"); // sub

  std::function<void(int, Entry*, Node*)> write_node;
  write_node = [&](int level, Entry *ent, Node *node) {
    bool keyed = (initial || !node->serialized);
    bool value_only = (!keyed && node->subs.empty());

    if (!value_only) {
      db.push('{');

      if (keyed) {
        db.push(s_k);
        db.push('"');
        if (level > 0) {
          utils::escape(node->key->str(), [&](char c) { db.push(c); });
        } else {
          utils::escape(ent->name->str(), [&](char c) { db.push(c); });
          db.push('"');
          db.push(',');
          db.push(s_t);
          db.push('"');
          utils::escape(ent->type->str(), [&](char c) { db.push(c); });
          db.push('"');
          db.push(',');
          db.push(s_l);
          db.push('"');
          utils::escape(ent->shape->str(), [&](char c) { db.push(c); });
        }
        db.push('"');
        db.push(',');
      }

      db.push(s_v);
    }

    int dim = ent->dimensions;
    if (dim > 1) db.push('[');

    for (int d = 0; d < dim; d++) {
      if (d > 0) db.push(',');
      char buf[100];
      auto len = pjs::Number::to_string(buf, sizeof(buf), node->values[d]);
      db.push(buf, len);
    }

    if (dim > 1) db.push(']');

    if (!node->subs.empty()) {
      db.push(',');
      db.push(s_s);
      db.push('[');
      bool first = true;
      for (auto *s = node->subs.head(); s; s = s->next()) {
        if (first) first = false; else db.push(',');
        write_node(level + 1, ent, s);
      }
      db.push(']');
    }

    if (!value_only) db.push('}');
    node->serialized = true;
  };

  db.push('{');
  db.push(s_metrics);
  db.push('[');
  for (auto *e = m_entries.head(); e; e = e->next()) {
    auto *root = e->root.get();
    if (e->back()) db.push(',');
    write_node(0, e, root);
  }
  db.push(']');
  db.push('}');
}

void MetricDataSum::to_prometheus(const std::function<void(const void *, size_t)> &out) const {
  static const std::string s_prefix_TYPE("# TYPE ");
  static const std::string s_type_counter(" counter\n");
  static const std::string s_type_gauge(" gauge\n");
  static const std::string s_type_histogram(" histogram\n");
  auto print = [&](const std::string &str) { out(str.c_str(), str.length()); };
  for (const auto &p : m_entry_map) {
    auto ent = p.second;
    if (auto root = ent->root.get()) {
      print(s_prefix_TYPE);
      print(ent->name->str());
      const char *le_str = nullptr;
      if (utils::starts_with(ent->type->str(), s_prefix_histogram)) {
        le_str = ent->type->c_str() + s_prefix_histogram.length();
        print(s_type_histogram);
      } else if (ent->type->str() == "Gauge") {
        print(s_type_gauge);
      } else {
        print(s_type_counter);
      }
      if (ent->shape->size() > 0 && ent->labels.empty()) {
        auto labels = utils::split(ent->shape->str(), '/');
        ent->labels.resize(labels.size());
        int i = 0;
        for (auto &s : labels) { ent->labels[i++] = std::move(s); }
      }
      pjs::Str::CharData *label_values[ent->labels.size()];
      std::string empty;
      Prometheus<Node> prom(ent->name->str(), empty, ent->labels, label_values, le_str, out);
      prom.output(root, 0);
    }
  }
}

//
// MetricDataSum::Node
//

auto MetricDataSum::Node::make(int dimensions) -> Node* {
  auto len = sizeof(Node) + (dimensions - 1) * sizeof(double);
  auto ptr = (Node *)std::calloc(len, 1);
  new (ptr) Node;
  return ptr;
}

MetricDataSum::Node::~Node() {
  auto *s = subs.head();
  while (s) {
    auto sub = s; s = s->next();
    delete sub;
  }
}

void MetricDataSum::Node::zero(int dimensions) {
  has_value = false;
  std::memset(values, 0, sizeof(values[0]) * dimensions);
  for (auto sub = subs.head(); sub; sub = sub->next()) {
    sub->zero(dimensions);
  }
}

//
// MetricHistory
//

MetricHistory::~MetricHistory() {
  for (const auto &i : m_entries) {
    delete i.second;
  }
}

void MetricHistory::step(MetricData &data) {
  step();

  int i = m_current % m_duration;

  std::function<void(int, Node*, MetricData::Node*)> update;
  update = [&](int dimensions, Node *node, MetricData::Node* src_node) {
    int base = i * dimensions;
    for (int d = 0; d < dimensions; d++) {
      node->values[base + d] = src_node->values[d];
    }

    auto &submap = node->submap;
    for (auto s = src_node->subs; s; s = s->next) {
      auto *key = pjs::Str::make(s->key)->retain();
      Node *sub = nullptr;
      auto it = submap.find(key);
      if (it == submap.end()) {
        submap[key] = sub = Node::make(dimensions, m_duration);
        sub->key = key;
      } else {
        sub = it->second;
      }
      key->release();
      update(dimensions, sub, s);
    }
  };

  for (auto *e = data.m_entries; e; e = e->next) {
    auto *name = pjs::Str::make(e->name)->retain();
    auto *type = pjs::Str::make(e->type)->retain();
    auto *shape = pjs::Str::make(e->shape)->retain();

    auto &ent = m_entries[name];
    if (!ent ||
      ent->type != type ||
      ent->shape != shape ||
      ent->dimensions != e->dimensions
    ) {
      if (!ent) ent = new Entry;
      ent->name = name;
      ent->type = type;
      ent->shape = shape;
      ent->dimensions = e->dimensions;
      ent->root.reset(Node::make(ent->dimensions, m_duration));
    }

    name->release();
    type->release();
    shape->release();

    update(ent->dimensions, ent->root.get(), e->root.get());
  }
}

void MetricHistory::step(MetricDataSum &data) {
  step();

  int i = m_current % m_duration;

  std::function<void(int, Node*, MetricDataSum::Node*)> update;
  update = [&](int dimensions, Node *node, MetricDataSum::Node *src_node) {
    int base = i * dimensions;
    for (int d = 0; d < dimensions; d++) {
      node->values[base + d] = src_node->values[d];
    }

    auto &submap = node->submap;
    for (auto s = src_node->subs.head(); s; s = s->next()) {
      auto *key = s->key.get();
      Node *sub = nullptr;
      auto it = submap.find(key);
      if (it == submap.end()) {
        submap[key] = sub = Node::make(dimensions, m_duration);
        sub->key = key;
      } else {
        sub = it->second;
      }
      update(dimensions, sub, s);
    }
  };

  for (auto *e = data.m_entries.head(); e; e = e->next()) {
    auto *name = e->name.get();
    auto *type = e->type.get();
    auto *shape = e->shape.get();

    auto &ent = m_entries[name];
    if (!ent ||
      ent->type != type ||
      ent->shape != shape ||
      ent->dimensions != e->dimensions
    ) {
      if (!ent) ent = new Entry;
      ent->name = name;
      ent->type = type;
      ent->shape = shape;
      ent->dimensions = e->dimensions;
      ent->root.reset(Node::make(ent->dimensions, m_duration));
    }

    update(ent->dimensions, ent->root.get(), e->root.get());
  }
}

void MetricHistory::step() {
  auto old = m_current;
  auto cur = old + 1;
  m_current = cur;

  if (cur - m_start >= m_duration) {
    m_start = cur - (m_duration - 1);
  }
}

void MetricHistory::serialize(Data::Builder &db) {
  bool first = true;
  db.push('[');
  for (const auto &i : m_entries) {
    auto entry = i.second;
    if (auto node = entry->root.get()) {
      if (first) first = false; else db.push(',');
      serialize(db, entry, node, 0, false);
    }
  }
  db.push(']');
}

void MetricHistory::serialize(Data::Builder &db, const std::string &metric_name) {
  pjs::Ref<pjs::Str> k(pjs::Str::make(metric_name));
  db.push('[');
  auto i = m_entries.find(k);
  if (i != m_entries.end()) {
    auto entry = i->second;
    if (auto node = entry->root.get()) {
      serialize(db, entry, node, 0, true);
    }
  }
  db.push(']');
}

void MetricHistory::serialize(Data::Builder &db, Entry *entry, Node *node, int level, bool recursive) {
  static const std::string s_k("\"k\":"); // key
  static const std::string s_t("\"t\":"); // type
  static const std::string s_v("\"v\":"); // value
  static const std::string s_l("\"l\":"); // label
  static const std::string s_s("\"s\":"); // sub

  db.push('{');
  db.push(s_k);
  db.push('"');
  if (level > 0) {
    utils::escape(node->key->str(), [&](char c) { db.push(c); });
  } else {
    utils::escape(entry->name->str(), [&](char c) { db.push(c); });
    db.push('"');
    db.push(',');
    db.push(s_t);
    db.push('"');
    utils::escape(entry->type->str(), [&](char c) { db.push(c); });
    db.push('"');
    db.push(',');
    db.push(s_l);
    db.push('"');
    utils::escape(entry->shape->str(), [&](char c) { db.push(c); });
  }
  db.push('"');
  db.push(',');
  db.push(s_v);

  auto dim = entry->dimensions;
  if (dim > 1) db.push('[');

  for (int d = 0; d < dim; d++) {
    if (d > 0) db.push(',');
    db.push('[');
    for (auto i = m_start; i <= m_current; i++) {
      if (i > m_start) db.push(',');
      char buf[100];
      auto len = pjs::Number::to_string(buf, sizeof(buf), node->values[i % m_duration]);
      db.push(buf, len);
    }
    db.push(']');
  }

  if (dim > 1) db.push(']');

  if (recursive && !node->submap.empty()) {
    db.push(',');
    db.push(s_s);
    db.push('[');

    bool first = true;
    for (const auto &i : node->submap) {
      if (first) first = false; else db.push(',');
      serialize(db, entry, i.second, level + 1, true);
    }

    db.push(']');
  }

  db.push('}');
}

//
// MetricHistory::Node
//

auto MetricHistory::Node::make(int dimensions, int duration) -> Node* {
  auto len = sizeof(Node) + (dimensions * duration - 1) * sizeof(double);
  auto ptr = (Node *)std::calloc(len, 1);
  new (ptr) Node;
  return ptr;
}

MetricHistory::Node::~Node() {
  for (const auto &i : submap) {
    delete i.second;
  }
}

//
// Counter
//

Counter::Counter(pjs::Str *name, pjs::Array *label_names, const std::function<void(Counter*)> &on_collect, MetricSet *set)
  : MetricTemplate<Counter>(name, label_names, set)
  , m_on_collect(on_collect)
{
}

Counter::Counter(Metric *parent, pjs::Str **labels)
  : MetricTemplate<Counter>(parent, labels)
{
}

auto Counter::get_type() -> pjs::Str* {
  return s_str_Counter.get();
}

void Counter::zero() {
  create_value();
  m_value = 0;
}

void Counter::increase(double n) {
  create_value();
  m_value += n;
}

//
// Gauge
//

Gauge::Gauge(pjs::Str *name, pjs::Array *label_names, const std::function<void(Gauge*)> &on_collect, MetricSet *set)
  : MetricTemplate<Gauge>(name, label_names, set)
  , m_on_collect(on_collect)
{
}

Gauge::Gauge(Metric *parent, pjs::Str **labels)
  : MetricTemplate<Gauge>(parent, labels)
{
}

auto Gauge::get_type() -> pjs::Str* {
  return s_str_Gauge.get();
}

void Gauge::zero() {
  create_value();
  m_value = 0;
}

void Gauge::set(double n) {
  create_value();
  m_value = n;
}

void Gauge::increase(double n) {
  create_value();
  m_value += n;
}

void Gauge::decrease(double n) {
  create_value();
  m_value -= n;
}

//
// Histogram
//

Histogram::Histogram(pjs::Str *name, pjs::Array *buckets, pjs::Array *label_names, MetricSet *set)
  : MetricTemplate<Histogram>(name, label_names, set)
{
  m_buckets = buckets;
  m_percentile = algo::Percentile::make(buckets);
  m_labels.resize(buckets->length());
  int i = 0;
  m_percentile->dump(
    [&](double bucket, double) {
      m_labels[i++] = pjs::Str::make(bucket);
    }
  );
}

Histogram::Histogram(Metric *parent, pjs::Str **labels)
  : MetricTemplate<Histogram>(parent, labels)
{
  auto root = static_cast<Histogram*>(parent);
  if (auto *r = root->m_root.get()) root = r;
  m_root = root;
  m_percentile = algo::Percentile::make(root->m_buckets);
}

void Histogram::zero() {
  m_sum = 0;
  m_count = 0;
  m_percentile->reset();
  create_value();
}

void Histogram::observe(double n) {
  m_sum += n;
  m_count++;
  m_percentile->observe(n);
  create_value();
}

void Histogram::value_of(pjs::Value &out) {
  const auto &labels = m_root ? m_root->m_labels : m_labels;
  auto *a = pjs::Array::make(labels.size());
  int i = 0;
  m_percentile->dump(
    [&](double, double count) {
      a->set(i++, count);
    }
  );
  out.set(a);
}

auto Histogram::get_type() -> pjs::Str* {
  std::string type;
  m_buckets->iterate_all(
    [&](pjs::Value &v, int) {
      if (type.empty()) {
        type = "Histogram[";
      } else {
        type += ',';
      }
      auto n = v.to_number();
      if (std::isnan(n)) {
        type += "\"NaN\"";
      } else if (std::isinf(n)) {
        type += n > 0 ? "\"Inf\"" : "\"-Inf\"";
      } else {
        char str[100];
        auto len = pjs::Number::to_string(str, sizeof(str), v.to_number());
        type += std::string(str, len);
      }
    }
  );
  type += ']';
  return pjs::Str::make(std::move(type));
}

auto Histogram::get_dim() -> int {
  return m_percentile->size() + 2;
}

auto Histogram::get_value(int dim) -> double {
  int size = m_percentile->size();
  if (0 <= dim && dim < size) {
    return m_percentile->get(dim);
  }
  switch (dim - size) {
    case 0: return m_count;
    case 1: return m_sum;
  }
  return 0;
}

void Histogram::set_value(int dim, double value) {
  int size = m_percentile->size();
  if (0 <= dim && dim < size) {
    m_percentile->set(dim - 1, value);
  }
  switch (dim - size) {
    case 0: m_count = value; break;
    case 1: m_sum = value; break;
  }
  create_value();
}

} // namespace stats
} // namespace pipy

namespace pjs {

using namespace pipy;
using namespace pipy::stats;

//
// Metric
//

template<> void ClassDef<Metric>::init() {
  accessor("name", [](Object *obj, Value &val) {
    val.set(obj->as<Metric>()->name());
  });

  method("withLabels", [](Context &ctx, Object *obj, Value &ret) {
    auto n = ctx.argc();
    pjs::Str *labels[n];
    for (int i = 0; i < n; i++) {
      auto *s = ctx.arg(i).to_string();
      labels[i] = s;
    }
    auto *metric = obj->as<Metric>()->with_labels(labels, n);
    for (int i = 0; i < n; i++) labels[i]->release();
    ret.set(metric);
  });

  method("clear", [](Context &ctx, Object *obj, Value &ret) {
    obj->as<Metric>()->clear();
  });
}

//
// Counter
//

template<> void ClassDef<Counter>::init() {
  super<Metric>();

  ctor([](Context &ctx) -> Object* {
    Str *name;
    Array *labels = nullptr;
    if (!ctx.arguments(1, &name, &labels)) return nullptr;
    return Counter::make(name, labels);
  });

  method("zero", [](Context &ctx, Object *obj, Value &ret) {
    obj->as<Counter>()->zero();
  });

  method("increase", [](Context &ctx, Object *obj, Value &ret) {
    double n = 1;
    if (!ctx.arguments(0, &n)) return;
    obj->as<Counter>()->increase(n);
  });
}

template<> void ClassDef<Constructor<Counter>>::init() {
  super<Function>();
  ctor();
}

//
// Gauge
//

template<> void ClassDef<Gauge>::init() {
  super<Metric>();

  ctor([](Context &ctx) -> Object* {
    Str *name;
    Array *labels = nullptr;
    Function *on_collect = nullptr;
    if (!ctx.arguments(1, &name, &labels, &on_collect)) return nullptr;
    if (!on_collect) return Gauge::make(name, labels);
    Ref<Function> f(on_collect);
    return Gauge::make(
      name, labels,
      [=](Gauge *g) {
        Context ctx(nullptr, Worker::current()->global_object());
        Value arg(g), ret;
        (*f)(ctx, 1, &arg, ret);
        if (!ctx.ok()) {
          Log::pjs_error(ctx.error());
        }
      }
    );
  });

  method("zero", [](Context &ctx, Object *obj, Value &ret) {
    obj->as<Gauge>()->zero();
  });

  method("set", [](Context &ctx, Object *obj, Value &ret) {
    double n;
    if (!ctx.arguments(1, &n)) return;
    obj->as<Gauge>()->set(n);
  });

  method("increase", [](Context &ctx, Object *obj, Value &ret) {
    double n = 1;
    if (!ctx.arguments(0, &n)) return;
    obj->as<Gauge>()->increase(n);
  });

  method("decrease", [](Context &ctx, Object *obj, Value &ret) {
    double n = 1;
    if (!ctx.arguments(0, &n)) return;
    obj->as<Gauge>()->decrease(n);
  });
}

template<> void ClassDef<Constructor<Gauge>>::init() {
  super<Function>();
  ctor();
}

//
// Histogram
//

template<> void ClassDef<Histogram>::init() {
  super<Metric>();

  ctor([](Context &ctx) -> Object* {
    Str *name;
    Array *buckets;
    Array *labels = nullptr;
    if (!ctx.check(0, name)) return nullptr;
    if (!ctx.check(1, buckets)) return nullptr;
    if (!ctx.check(2, labels, labels)) return nullptr;
    try {
      return Histogram::make(name, buckets, labels);
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });

  method("zero", [](Context &ctx, Object *obj, Value &ret) {
    obj->as<Histogram>()->zero();
  });

  method("observe", [](Context &ctx, Object *obj, Value &ret) {
    double n;
    if (!ctx.arguments(1, &n)) return;
    obj->as<Histogram>()->observe(n);
  });
}

template<> void ClassDef<Constructor<Histogram>>::init() {
  super<Function>();
  ctor();
}

//
// Stats
//

template<> void ClassDef<Stats>::init() {
  ctor();
  variable("Counter", class_of<Constructor<Counter>>());
  variable("Gauge", class_of<Constructor<Gauge>>());
  variable("Histogram", class_of<Constructor<Histogram>>());
}

} // namespace pjs
