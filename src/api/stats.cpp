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

namespace pipy {
namespace stats {

static Data::Producer s_dp("Stats");

//
// Metric
//

auto Metric::local() -> MetricSet& {
  static MetricSet s_local_metric_set;
  return s_local_metric_set;
}

Metric::Metric(pjs::Str *name, pjs::Array *label_names, MetricSet *set)
  : m_root(nullptr)
  , m_name(name)
  , m_label_index(-1)
  , m_label_names(std::make_shared<std::vector<pjs::Ref<pjs::Str>>>())
{
  if (label_names) {
    auto n = label_names->length();
    m_label_names->resize(n);
    for (auto i = 0; i < n; i++) {
      pjs::Value v; label_names->get(i, v);
      auto *s = v.to_string();
      if (!m_shape.empty()) m_shape += '/';
      m_shape += s->str();
      m_label_names->at(i) = s;
      s->release();
    }
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

void Metric::serialize(Data::Builder &db, bool initial) {
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
        utils::escape(shape(), [&](char c) { db.push(c); });
        db.push('"');
        db.push(',');
        db.push(s_t);
        db.push('"');
        utils::escape(type(), [&](char c) { db.push(c); });
      }
      db.push('"');
      db.push(',');
    }

    db.push(s_v);
  }

  bool first_dimension = true;
  dump(
    [&](pjs::Str *dim, double x) {
      if (dim) {
        if (first_dimension) {
          db.push('[');
          first_dimension = false;
        } else {
          db.push(',');
        }
      }
      char buf[100];
      auto len = pjs::Number::to_string(buf, sizeof(buf), x);
      db.push(buf, len);
    }
  );

  if (!first_dimension) db.push(']');

  if (!m_subs.empty()) {
    db.push(',');
    db.push(s_s);
    db.push('[');
    bool first = true;
    for (const auto &i : m_subs) {
      if (first) first = false; else db.push(',');
      i->serialize(db, initial);
    }
    db.push(']');
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
    while (i < m_subs.size()) {
      auto metric = m_subs[i++].get();
      m_sub_map.erase(metric->label());
    }
    m_subs.resize(i);
  }
}

void Metric::dump_tree(
  pjs::Str **label_names,
  pjs::Str **label_values,
  const std::function<void(int, double)> &out
) {
  int i = m_label_index;
  if (i >= 0) {
    label_names[i] = m_label_names->at(i);
    label_values[i] = m_label;
  }
  if (m_has_value) {
    dump(
      [&](pjs::Str *dim, double x) {
        if (dim) {
          label_names[i+1] = pjs::Str::empty;
          label_values[i+1] = dim;
          out(i+2, x);
        } else {
          out(i+1, x);
        }
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
  return i->second;
}

auto MetricSet::get(int i) -> Metric* {
  if (0 <= i && i < m_metrics.size()) {
    return m_metrics[i].get();
  } else {
    return nullptr;
  }
}

void MetricSet::add(Metric *metric) {
  m_metrics.emplace_back();
  m_metrics.back() = metric;
  m_metric_map[metric->name()] = metric;
}

void MetricSet::truncate(int i) {
  if (0 <= i && i < m_metrics.size()) {
    while (i < m_metrics.size()) {
      auto metric = m_metrics[i++].get();
      m_metric_map.erase(metric->name());
    }
    m_metrics.resize(i);
  }
}

void MetricSet::collect_all() {
  for (const auto &m : m_metrics) {
    m->collect();
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
    metric->serialize(db, initial);
  }
  db.push(']');
  db.push('}');
  db.flush();
}

void MetricSet::deserialize(Data &in) {
}

void MetricSet::to_prometheus(Data &out) {
  static std::string le("le");
  Data::Builder db(out, &s_dp);
  for (const auto &metric : m_metrics) {
    auto name = metric->name();
    auto max_dim = metric->m_label_names->size() + 1;
    pjs::Str *label_names[max_dim];
    pjs::Str *label_values[max_dim];
    metric->dump_tree(
      label_names,
      label_values,
      [&](int dim, double x) {
        db.push(name->str());
        if (dim > 0) {
          for (int i = 0; i < dim; i++) {
            auto label_name = label_names[i];
            db.push(i ? ',' : '{');
            db.push(label_name->size() > 0 ? label_name->str() : le);
            db.push('=');
            db.push('"');
            db.push(label_values[i]->str());
            db.push('"');
          }
          db.push('}');
        }
        char buf[100];
        auto len = pjs::Number::to_string(buf, sizeof(buf), x);
        db.push(' ');
        db.push(buf, len);
        db.push('\n');
      }
    );
  }
  db.flush();
}

//
// MetricSet::Deserializer
//

MetricSet::Deserializer::~Deserializer() {
  while (m_current) {
    pop();
  }
}

void MetricSet::Deserializer::push(Level *level) {
  level->parent = m_current;
  m_current = level;
}

void MetricSet::Deserializer::pop() {
  if (auto *level = m_current) {
    m_current = level->parent;
    delete level;
  }
}

auto MetricSet::Deserializer::open() -> Metric* {
  if (auto *level = m_current) {
    if (level->id == Level::ID::VALUE) level = level->parent;
    if (level && level->id == Level::ID::INDEX) {
      auto i = ++level->index;
      if (auto parent = level->parent) {
        switch (parent->id) {
          case Level::ID::METRICS: {
            auto metric = m_metric_set->get(i);
            if (metric && (metric->name() != level->key || metric->shape() != level->shape || metric->type() != level->type)) {
              m_metric_set->truncate(i);
              metric = nullptr;
            }
            if (!metric) {
              // TODO: create a new metric
            }
            m_current->metric = metric;
            return metric;
          }
          case Level::ID::SUB: {
            auto metric = parent->metric->get_sub(i);
            if (metric && metric->label() != level->key) {
              parent->metric->truncate(i);
              metric = nullptr;
            }
            if (!metric) {
              auto *key = parent->key.get();
              metric = parent->metric->with_labels(&key, 1);
            }
            m_current->metric = metric;
            return metric;
          }
          default: error(); return nullptr;
        }
      }
    }
  }
  error();
  return nullptr;
}

void MetricSet::Deserializer::error() {
  m_has_error = true;
}

void MetricSet::Deserializer::null() {
  error();
}

void MetricSet::Deserializer::boolean(bool b) {
  error();
}

void MetricSet::Deserializer::integer(int64_t i) {
  number(i);
}

void MetricSet::Deserializer::number(double n) {
  if (m_has_error) return;
  if (auto *metric = open()) {
    metric->set_value(0, n);
  }
}

void MetricSet::Deserializer::string(const char *s, size_t len) {
  if (m_has_error) return;
  if (auto *level = m_current) {
    switch (level->id) {
      case Level::ID::UUID:
        m_uuid.assign(s, len);
        break;
      case Level::ID::KEY:
        level->key = pjs::Str::make(s, len);
        break;
      case Level::ID::LABELS:
        level->shape.assign(s, len);
        break;
      case Level::ID::TYPE:
        level->type.assign(s, len);
        break;
      default: error(); break;
    }
  } else {
    error();
  }
}

void MetricSet::Deserializer::map_start() {
  if (m_has_error) return;
  push(new Level(Level::ID::NONE));
}

void MetricSet::Deserializer::map_key(const char *s, size_t len) {
  if (m_has_error) return;
  if (len == 1) {
    switch (*s) {
      case 'k': m_current->id = Level::ID::KEY; break;
      case 'l': m_current->id = Level::ID::LABELS; break;
      case 't': m_current->id = Level::ID::TYPE; break;
      case 'v': m_current->id = Level::ID::VALUE; break;
      case 's': m_current->id = Level::ID::SUB; break;
      default: error(); break;
    }
  } else if (m_current && !m_current->parent) {
    if (!std::strcmp(s, "uuid")) m_current->id = Level::ID::UUID;
    else if (!std::strcmp(s, "metrics")) m_current->id = Level::ID::METRICS;
    else error();
  } else {
    error();
  }
}

void MetricSet::Deserializer::map_end() {
  if (!m_has_error) {
    pop();
  }
}

void MetricSet::Deserializer::array_start() {
  if (m_has_error) return;
  push(new Level(Level::ID::INDEX));
}

void MetricSet::Deserializer::array_end() {
  if (!m_has_error) {
    pop();
  }
}

//
// Counter
//

Counter::Counter(pjs::Str *name, pjs::Array *label_names)
  : MetricTemplate<Counter>(name, label_names)
{
}

Counter::Counter(Metric *parent, pjs::Str **labels)
  : MetricTemplate<Counter>(parent, labels)
{
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

Gauge::Gauge(pjs::Str *name, pjs::Array *label_names, const std::function<void(Gauge*)> &on_collect)
  : MetricTemplate<Gauge>(name, label_names)
  , m_on_collect(on_collect)
{
}

Gauge::Gauge(Metric *parent, pjs::Str **labels)
  : MetricTemplate<Gauge>(parent, labels)
{
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

Histogram::Histogram(pjs::Str *name, pjs::Array *buckets, pjs::Array *label_names)
  : MetricTemplate<Histogram>(name, label_names)
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
  create_value();
  m_percentile->reset();
}

void Histogram::observe(double n) {
  create_value();
  m_percentile->observe(n);
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

auto Histogram::get_type() -> const std::string& {
  if (m_type.empty()) {
    m_buckets->iterate_all(
      [this](pjs::Value &v, int) {
        if (m_type.empty()) {
          m_type = "Histogram[";
        } else {
          m_type += ',';
        }
        char str[100];
        auto len = pjs::Number::to_string(str, sizeof(str), v.to_number());
        m_type += std::string(str, len);
      }
    );
    m_type += ']';
  }
  return m_type;
}

void Histogram::set_value(int dim, double value) {
  m_percentile->set(dim, value);
}

void Histogram::dump(const std::function<void(pjs::Str*, double)> &out) {
  const auto &labels = m_root ? m_root->m_labels : m_labels;
  int i = 0;
  m_percentile->dump(
    [&](double, double count) {
      out(labels[i++], count);
    }
  );
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
    return Histogram::make(name, buckets, labels);
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
