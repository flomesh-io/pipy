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
#include "utils.hpp"
#include "logging.hpp"

namespace pipy {
namespace stats {

static Data::Producer s_dp("Stats");
static pjs::ConstStr s_str_count("count");
static pjs::ConstStr s_str_sum("sum");

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

void MetricSet::deserialize(
  const Data &in,
  const std::function<MetricSet*(const std::string &uuid)> &by_uuid
) {
  Deserializer des(by_uuid);
  if (!JSON::visit(in, &des)) {
    Log::error("[stats] JSON deserialization failed for metrics");
    return;
  }
  if (des.has_error()) {
    Log::error("[stats] Invalid JSON structure for metrics");
  }
}

void MetricSet::to_prometheus(Data &out, const std::string &inst) const {
  static std::string s_le("le=");
  static std::string s_bucket("_bucket");
  static std::string s_sum("_sum");
  static std::string s_count("_count");

  Data::Builder db(out, &s_dp);
  for (const auto &metric : m_metrics) {
    auto name = metric->name();
    auto max_dim = metric->m_label_names->size() + 1;
    pjs::Str *label_names[max_dim];
    pjs::Str *label_values[max_dim];
    metric->dump_tree(
      label_names,
      label_values,
      [&](int depth, pjs::Str *dim, double x) {
        db.push(name->str());
        bool has_le = false;
        if (dim == s_str_sum) {
          db.push(s_sum);
        } else if (dim == s_str_count) {
          db.push(s_count);
        } else if (dim) {
          db.push(s_bucket);
          has_le = true;
        }
        if (depth > 0 || has_le || !inst.empty()) {
          bool first = true;
          if (!inst.empty()) {
            db.push('{');
            db.push(inst);
            first = false;
          }
          for (int i = 0; i < depth; i++) {
            auto label_name = label_names[i];
            db.push(first ? '{' : ',');
            db.push(label_name->str());
            db.push('=');
            db.push('"');
            db.push(label_values[i]->str());
            db.push('"');
            first = false;
          }
          if (has_le) {
            db.push(first ? '{' : ',');
            db.push(s_le);
            db.push('"');
            db.push(dim->str());
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

auto MetricSet::Deserializer::open(Level *current, Level *list, pjs::Str *key) -> Metric* {
  if (list) {
    if (auto parent = list->parent) {
      auto i = ++list->index;
      switch (parent->id) {

        // Root metric
        case Level::ID::METRICS: {
          if (m_metric_set) {
            auto m = m_metric_set->get(i);
            if (m && key) {
              if (
                m->name() != key ||
                m->shape() != current->shape ||
                m->type() != current->type
              ) {
                m_metric_set->truncate(i);
                m = nullptr;
              }
            }

            if (!m && key) {
              static std::string s_histogram("Histogram[");

              auto labels = pjs::Array::make();
              for (const auto &s : utils::split(current->shape, '/')) {
                labels->push(s);
              }

              if (current->type == "Counter") {
                m = Counter::make(key, labels, m_metric_set);
              } else if (current->type == "Gauge") {
                m = Gauge::make(key, labels, nullptr, m_metric_set);
              } else if (utils::starts_with(current->type, s_histogram)) {
                auto buckets_str = current->type.substr(s_histogram.length());
                auto buckets = pjs::Array::make();
                for (const auto &s : utils::split(buckets_str, ',')) {
                  auto n = std::atof(s.c_str());
                  buckets->push(n);
                }
                m = Histogram::make(key, buckets, labels, m_metric_set);
              }
            }

            if (m) {
              current->metric = m;
              return m;
            }
          }
          break;
        }

        // Sub metric
        case Level::ID::SUB: {
          auto p = parent->metric;
          auto m = p->get_sub(i);
          if (m && key && m->label() != key) {
            p->truncate(i);
            m = nullptr;
          }
          if (!m && key) {
            m = p->with_labels(&key, 1);
          }
          current->metric = m;
          return m;
        }

        default: break;
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
  if (auto *current = m_current) {
    auto *list = current;
    pjs::Str *key = nullptr;
    if (current->id == Level::ID::VALUE) {
      list = current->parent;
      key = current->key.get();
    }

    if (list && list->id == Level::ID::INDEX) {
      if (auto parent = list->parent) {
        switch (parent->id) {
          case Level::ID::METRICS:
          case Level::ID::SUB:
            if (auto m = open(current, list, key)) {
              m->set_value(0, n);
              return;
            }
            break;
          case Level::ID::VALUE:
          case Level::ID::INDEX:
            if (auto m = parent->metric) {
              m->set_value(++list->index, n);
              return;
            }
            break;
          default: break;
        }
      }
    }
  }
  error();
}

void MetricSet::Deserializer::string(const char *s, size_t len) {
  if (m_has_error) return;
  if (auto *level = m_current) {
    switch (level->id) {
      case Level::ID::UUID:
        if (!m_metric_set) {
          m_metric_set = m_by_uuid(std::string(s, len));
        }
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
  if (!m_has_error) {
    push(new Level(Level::ID::NONE));
  }
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
    if (
      m_current->id == Level::ID::LABELS ||
      m_current->id == Level::ID::TYPE
    ) {
      auto p1 = m_current->parent;
      auto p2 = p1 ? p1->parent : nullptr;
      if (!p1 || p1->id != Level::ID::INDEX ||
          !p2 || p2->id != Level::ID::METRICS
      ) {
        error();
      }
    }
  } else if (m_current && !m_current->parent) {
    if (!std::strncmp(s, "uuid", len)) m_current->id = Level::ID::UUID;
    else if (!std::strncmp(s, "metrics", len)) m_current->id = Level::ID::METRICS;
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
  if (auto *level = m_current) {
    if (level->id == Level::ID::VALUE) {
      open(level, level->parent, level->key);
    } else if (level->id == Level::ID::INDEX) {
      open(level, level, nullptr);
    }
  }
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

Counter::Counter(pjs::Str *name, pjs::Array *label_names, MetricSet *set)
  : MetricTemplate<Counter>(name, label_names, set)
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

Gauge::Gauge(pjs::Str *name, pjs::Array *label_names, const std::function<void(Gauge*)> &on_collect, MetricSet *set)
  : MetricTemplate<Gauge>(name, label_names, set)
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
