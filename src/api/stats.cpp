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
#include "compress.hpp"
#include "utils.hpp"
#include "log.hpp"

#include <cmath>

namespace pipy {
namespace stats {

static Data::Producer s_dp("Stats");
thread_local static pjs::ConstStr s_str_Counter("Counter");
thread_local static pjs::ConstStr s_str_Gauge("Gauge");
thread_local static pjs::ConstStr s_str_count("count");
thread_local static pjs::ConstStr s_str_sum("sum");

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

void Metric::history_step() {
  auto i = (m_history_end++) % MAX_HISTORY;
  if (m_history_end - m_history_start > MAX_HISTORY) {
    m_history_start = m_history_end - MAX_HISTORY;
  }

  int dim = get_dim();
  if (m_history.size() < dim) m_history.resize(dim);
  for (int d = 0; d < dim; d++) {
    auto v = get_value(d);
    m_history[d].v[i] = v;
  }

  for (const auto &sub : m_subs) {
    sub->history_step();
  }
}

auto Metric::history(int dim, double *values) -> size_t {
  if (0 <= dim && dim < m_history.size()) {
    const auto &v = m_history[dim].v;
    size_t n = m_history_end - m_history_start;
    for (size_t i = 0; i < n; i++) {
      values[i] = v[(i + m_history_start) % MAX_HISTORY];
    }
    return n;
  } else {
    return 0;
  }
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

void Metric::serialize(Data::Builder &db, bool initial, bool recursive, bool history) {
  static std::string s_k("\"k\":"); // key
  static std::string s_t("\"t\":"); // type
  static std::string s_v("\"v\":"); // value
  static std::string s_l("\"l\":"); // label
  static std::string s_s("\"s\":"); // sub

  bool keyed = (initial || !m_has_serialized);
  bool value_only = (!keyed && m_subs.empty());

  if (!value_only) {
    db.push('{');

    if (keyed) {
      db.push(s_k);
      db.push('"');
      if (m_label_index >= 0) {
        utils::escape(m_label->str(), [&](char c) { db.push(c); });
      } else {
        utils::escape(m_name->str(), [&](char c) { db.push(c); });
        db.push('"');
        db.push(',');
        db.push(s_l);
        db.push('"');
        utils::escape(shape()->str(), [&](char c) { db.push(c); });
        db.push('"');
        db.push(',');
        db.push(s_t);
        db.push('"');
        utils::escape(type()->str(), [&](char c) { db.push(c); });
      }
      db.push('"');
      db.push(',');
    }

    db.push(s_v);
  }

  auto dim = get_dim();
  if (dim > 1) db.push('[');

  for (int d = 0; d < dim; d++) {
    if (d > 0) db.push(',');
    if (history) {
      auto n = history_size();
      double v[n];
      n = this->history(d, v);
      db.push('[');
      for (size_t i = 0; i < n; i++) {
        if (i > 0) db.push(',');
        char buf[100];
        auto len = pjs::Number::to_string(buf, sizeof(buf), v[i]);
        db.push(buf, len);
      }
      db.push(']');
    } else {
      char buf[100];
      auto len = pjs::Number::to_string(buf, sizeof(buf), get_value(d));
      db.push(buf, len);
    }
  }

  if (dim > 1) db.push(']');

  if (recursive) {
    if (!m_subs.empty()) {
      db.push(',');
      db.push(s_s);
      db.push('[');
      bool first = true;
      for (const auto &i : m_subs) {
        if (first) first = false; else db.push(',');
        i->serialize(db, initial, recursive, history);
      }
      db.push(']');
    }
  }

  if (!value_only) db.push('}');
  m_has_serialized = true;
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

void Metric::truncate(int i) {
  if (0 <= i && i < m_subs.size()) {
    auto n = i;
    while (i < m_subs.size()) {
      auto metric = m_subs[i++].get();
      m_sub_map.erase(metric->label());
    }
    m_subs.resize(n);
  }
}

void Metric::dump_tree(
  pjs::Str **label_names,
  pjs::Str **label_values,
  const std::function<void(int, pjs::Str*, double)> &out
) {
  int i = m_label_index;
  if (i >= 0) {
    label_names[i] = m_label_names->at(i);
    label_values[i] = m_label;
  }
  if (m_has_value) {
    dump(
      [&](pjs::Str *dim, double x) {
        out(i+1, dim, x);
      }
    );
  }
  for (const auto &i : m_subs) {
    i->dump_tree(
      label_names,
      label_values,
      out
    );
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

void MetricSet::truncate(int i) {
  if (0 <= i && i < m_metrics.size()) {
    auto n = i;
    while (i < m_metrics.size()) {
      auto metric = m_metrics[i++].get();
      m_metric_map.erase(metric->name());
    }
    m_metrics.resize(n);
  }
}

void MetricSet::collect_all() {
  for (const auto &m : m_metrics) {
    m->collect();
  }
}

void MetricSet::history_step() {
  for (const auto &m : m_metrics) {
    m->history_step();
  }
}

void MetricSet::serialize(Data &out, const std::string &uuid, bool initial) {
  static std::string s_uuid("\"uuid\":");
  static std::string s_metrics("\"metrics\":");
  Data::Builder db(out, &s_dp);
  db.push('{');
  db.push(s_uuid);
  db.push('"');
  db.push(uuid);
  db.push('"');
  db.push(',');
  db.push(s_metrics);
  db.push('[');
  bool first = true;
  for (const auto &metric : m_metrics) {
    if (first) first = false; else db.push(',');
    metric->serialize(db, initial, true, false);
  }
  db.push(']');
  db.push('}');
  db.flush();
}

void MetricSet::serialize_history(Data &out, const std::string &metric_name, std::chrono::time_point<std::chrono::steady_clock> timestamp) {
  static std::string s_time("\"time\":");
  static std::string s_metrics("\"metrics\":");
  auto time = std::chrono::duration_cast<std::chrono::seconds>(timestamp.time_since_epoch()).count();
  Data::Builder db(out, &s_dp);
  db.push('{');
  db.push(s_time);
  db.push(std::to_string(time));
  db.push(',');
  db.push(s_metrics);
  db.push('[');
  if (metric_name.empty()) {
    bool first = true;
    for (const auto &metric : m_metrics) {
      if (first) first = false; else db.push(',');
      metric->serialize(db, true, false, true);
    }
  } else {
    pjs::Ref<pjs::Str> k(pjs::Str::make(metric_name));
    if (auto *metric = get(k)) {
      metric->serialize(db, true, true, true);
    }
  }
  db.push(']');
  db.push('}');
  db.flush();
}

void MetricSet::to_prometheus(Data &out, const std::string &inst) const {
  Data::Builder db(out, &s_dp);
  to_prometheus(
    [&](const void *data, size_t size) {
      if (size == 1) {
        db.push(*(const char *)data);
      } else {
        db.push((const char *)data, size);
      }
    },
    inst
  );
  db.flush();
}

void MetricSet::to_prometheus(const std::function<void(const void *, size_t)> &out, const std::string &inst) const {
  static std::string s_le("le=");
  static std::string s_bucket("_bucket");
  static std::string s_sum("_sum");
  static std::string s_count("_count");

  auto push_c = [&](char c) {
    out(&c, 1);
  };

  auto push_d = [&](const void *data, size_t size) {
    out(data, size);
  };

  auto push_s = [&](const std::string &s) {
    out(s.c_str(), s.length());
  };

  for (const auto &metric : m_metrics) {
    auto name = metric->name();
    auto max_dim = metric->m_label_names->size() + 1;
    pjs::Str *label_names[max_dim];
    pjs::Str *label_values[max_dim];
    metric->dump_tree(
      label_names,
      label_values,
      [&](int depth, pjs::Str *dim, double x) {
        push_s(name->str());
        bool has_le = false;
        if (dim == s_str_sum) {
          push_s(s_sum);
        } else if (dim == s_str_count) {
          push_s(s_count);
        } else if (dim) {
          push_s(s_bucket);
          has_le = true;
        }
        if (depth > 0 || has_le || !inst.empty()) {
          bool first = true;
          if (!inst.empty()) {
            push_c('{');
            push_s(inst);
            first = false;
          }
          for (int i = 0; i < depth; i++) {
            auto label_name = label_names[i];
            push_c(first ? '{' : ',');
            push_s(label_name->str());
            push_c('=');
            push_c('"');
            push_s(label_values[i]->str());
            push_c('"');
            first = false;
          }
          if (has_le) {
            push_c(first ? '{' : ',');
            push_s(s_le);
            push_c('"');
            push_s(dim->str());
            push_c('"');
          }
          push_c('}');
        }
        char buf[100];
        auto len = pjs::Number::to_string(buf, sizeof(buf), x);
        push_c(' ');
        push_d(buf, len);
        push_c('\n');
      }
    );
  }
}

void MetricSet::clear() {
  m_metric_map.clear();
  m_metrics.clear();
}

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
  static std::function<void(int, Node*, Metric*)> update;

  update = [](int level, Node *node, Metric *metric) {
    if (level > 0) node->key.str(metric->label());

    auto dim = metric->dimensions();
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

    auto *s = *sub;
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
      e->name.str() != metric->name() ||
      e->type.str() != metric->type() ||
      e->shape.str() != metric->shape() ||
      e->dimensions != metric->dimensions()
    ) {
      delete e;
      e = *ent = new Entry;
      e->root.reset(Node::make(metric->dimensions()));
      e->name.str(metric->name());
      e->type.str(metric->type());
      e->shape.str(metric->shape());
      e->dimensions = metric->dimensions();
    }
    update(0, e->root.get(), metric);
    ent = &e->next;
  }

  auto *e = *ent;
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

//
// MetricData::Node
//

auto MetricData::Node::make(int dimensions) -> Node* {
  auto len = sizeof(Node) + (dimensions - 1) * sizeof(double);
  auto ptr = (Node *)std::malloc(len);
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
        auto is_entry = !(level->subs);
        pjs::Ref<pjs::Str> str(pjs::Str::make(s, len));
        switch (level->field) {
          case Level::Field::KEY:
            if (is_entry) {
              m_current_entry->name.str(str);
            } else {
              level->node->key.str(str);
            }
            return;
          case Level::Field::TYPE:
            if (is_entry) {
              static std::string s_prefix_histogram("Histogram[");
              int dim = 1;
              if (utils::starts_with(str->str(), s_prefix_histogram)) {
                for (auto c : str->str()) if (c == ',') dim++;
                dim += 2;
              }
              if (dim <= 100) {
                auto node = Node::make(dim);
                m_current_entry->type.str(str);
                m_current_entry->dimensions = dim;
                m_current_entry->root.reset(node);
                level->node = node;
                return;
              }
            }
            break;
          case Level::Field::LABELS:
            if (is_entry) {
              m_current_entry->shape.str(str);
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
  static std::function<void(int, Node*, MetricData::Node*)> sum;

  sum = [=](int dimensions, Node *node, MetricData::Node* src_node) {
    if (initial) {
      for (int i = 0; i < dimensions; i++) {
        node->values[i] = src_node->values[i];
      }
    } else {
      for (int i = 0; i < dimensions; i++) {
        node->values[i] += src_node->values[i];
      }
    }

    auto &submap = node->submap;
    for (auto s = src_node->subs; s; s = s->next) {
      auto *key = s->key.to_string();
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
    auto *name = e->name.to_string();
    auto *type = e->type.to_string();
    auto *shape = e->shape.to_string();

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
      ent->root.reset(Node::make(ent->dimensions));
    }

    name->release();
    type->release();
    shape->release();

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
  static std::string s_metrics("\"metrics\":"); // metrics
  static std::string s_k("\"k\":"); // key
  static std::string s_t("\"t\":"); // type
  static std::string s_v("\"v\":"); // value
  static std::string s_l("\"l\":"); // label
  static std::string s_s("\"s\":"); // sub

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

//
// MetricDataSum::Node
//

auto MetricDataSum::Node::make(int dimensions) -> Node* {
  auto len = sizeof(Node) + (dimensions - 1) * sizeof(double);
  auto ptr = (Node *)std::malloc(len);
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

//
// Counter
//

Counter::Counter(pjs::Str *name, pjs::Array *label_names, MetricSet *set)
  : MetricTemplate<Counter>(name, label_names, set)
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

void Histogram::dump(const std::function<void(pjs::Str*, double)> &out) {
  const auto &labels = m_root ? m_root->m_labels : m_labels;
  int i = 0;
  m_percentile->dump(
    [&](double, size_t count) {
      out(labels[i++], count);
    }
  );
  out(s_str_count, m_count);
  out(s_str_sum, m_sum);
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
    if (!ctx.arguments(1, &name, &labels)) return nullptr;
    return Gauge::make(name, labels);
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
