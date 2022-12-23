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

#ifndef STATS_HPP
#define STATS_HPP

#include "pjs/pjs.hpp"
#include "api/algo.hpp"
#include "api/json.hpp"
#include "data.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pipy {
namespace stats {

class MetricData;
class MetricDataSum;
class MetricHistory;
class MetricSet;

//
// Metric
//

class Metric : public pjs::ObjectTemplate<Metric> {
public:
  enum { MAX_HISTORY = 60 };

  static auto local() -> MetricSet&;

  auto root() const -> Metric* { return m_root; }
  auto name() const -> pjs::Str* { return m_name; }
  auto label() const -> pjs::Str* { return m_label; }
  auto shape() const -> pjs::Str* { return m_root ? m_root->m_shape : m_shape; }
  auto type() -> pjs::Str*;
  auto dimensions() -> int { return get_dim(); }
  auto with_labels(pjs::Str *const *labels, int count) -> Metric*;
  auto history_size() -> size_t { return m_history_end - m_history_start; }
  void history_step();
  auto history(int dim, double *values) -> size_t;
  void zero_all();
  void clear();

  virtual void zero() = 0;

protected:
  Metric(pjs::Str *name, pjs::Array *label_names, MetricSet *set = nullptr);
  Metric(Metric *parent, pjs::Str **labels);
  virtual ~Metric() {}

  bool has_value() const { return m_has_value; }
  void create_value();
  void serialize(Data::Builder &db, bool initial, bool recursive, bool history);

  virtual auto create_new(Metric *parent, pjs::Str **labels) -> Metric* = 0;
  virtual auto get_type() -> pjs::Str* = 0;
  virtual auto get_dim() -> int { return 1; }
  virtual auto get_value(int dim) -> double = 0;
  virtual void set_value(int dim, double value) = 0;
  virtual void collect() {}
  virtual void dump(const std::function<void(pjs::Str*, double)> &out) {};

private:
  struct HistoryValues {
    double v[MAX_HISTORY];
  };

  auto get_sub(pjs::Str **labels) -> Metric*;
  auto get_sub(int i) -> Metric*;
  void truncate(int i);

  void dump_tree(
    pjs::Str **label_names,
    pjs::Str **label_values,
    const std::function<void(int, pjs::Str*, double)> &out
  );

  Metric* m_root;
  pjs::Ref<pjs::Str> m_name;
  pjs::Ref<pjs::Str> m_type;
  pjs::Ref<pjs::Str> m_shape;
  pjs::Ref<pjs::Str> m_label;
  int m_label_index;
  bool m_has_value = false;
  bool m_has_serialized = false;
  std::shared_ptr<std::vector<pjs::Ref<pjs::Str>>> m_label_names;
  std::vector<pjs::Ref<Metric>> m_subs;
  std::unordered_map<pjs::Ref<pjs::Str>, Metric*> m_sub_map;
  std::vector<HistoryValues> m_history;
  size_t m_history_start = 0;
  size_t m_history_end = 0;

  friend class pjs::ObjectTemplate<Metric>;
  friend class MetricData;
  friend class MetricSet;
};

//
// MetricSet
//

class MetricSet {
public:
  auto get(pjs::Str *name) -> Metric*;
  void collect_all();
  void history_step();
  void serialize(Data &out, const std::string &uuid, bool initial);
  void serialize_history(Data &out, const std::string &metric_name, std::chrono::time_point<std::chrono::steady_clock> timestamp);
  void to_prometheus(const std::function<void(const void *, size_t)> &out, const std::string &inst) const;
  void to_prometheus(Data &out, const std::string &inst) const;
  void clear();

private:
  std::vector<pjs::Ref<Metric>> m_metrics;
  std::unordered_map<pjs::Ref<pjs::Str>, int> m_metric_map;

  auto get(int i) -> Metric*;
  void add(Metric *metric);
  void truncate(int i);

  friend class Metric;
  friend class MetricData;
};

//
// MetricData
//

class MetricData {
public:
  ~MetricData();

  void update(MetricSet &metrics);
  void deserialize(const Data &in);
  void to_prometheus(const std::string &inst, const std::function<void(const void *, size_t)> &out) const;
  void dump();

private:

  //
  // MetricData::Node
  //

  struct Node {
    pjs::Str::ID key;
    Node* subs = nullptr;
    Node* next = nullptr;
    double values[1];
    static auto make(int dimensions) -> Node*;
    ~Node();
  private:
    Node() {}
  };

  //
  // MetricData::Entry
  //

  struct Entry {
    pjs::Str::ID name;
    pjs::Str::ID type;
    pjs::Str::ID shape;
    int dimensions = 0;
    std::unique_ptr<Node> root;
    Entry* next = nullptr;
  };

  //
  // MetricData::Iterator
  //

  template<class T>
  class Iterator {
  public:
    Iterator(T** start) : m_ptr(start) {}
    operator bool() const { return m_ptr; }
    auto next(const std::function<T*()> &creator = nullptr) -> T* {
      auto obj = *m_ptr;
      if (!obj && creator) obj = *m_ptr = creator();
      if (obj) m_ptr = &obj->next;
      return obj;
    }
  private:
    T** m_ptr;
  };

  //
  // MetricData::Deserializer
  //

  class Deserializer : public JSON::Visitor {
  public:
    Deserializer(MetricData *metric_data) : m_entries(&metric_data->m_entries) {}
    ~Deserializer();

    bool has_error() const { return m_has_error; }

  private:
    struct Level : public pjs::Pooled<Level> {
      enum class Kind {
        ROOT,
        ENTRIES,
        SUBS,
        METRIC,
        VALUES,
      };

      enum class Field {
        NONE,
        METRICS,
        KEY,
        TYPE,
        LABELS,
        VALUE,
        SUB,
      };

      Level(Kind k, Node *n = nullptr, Node **s = nullptr)
        : kind(k)
        , node(n)
        , subs(s) {}

      Level* parent;
      Kind kind;
      Field field = Field::NONE;
      int index = 0;
      Node* node;
      Iterator<Node> subs;
    };

    Level* m_current_level = nullptr;
    Entry* m_current_entry = nullptr;
    Iterator<Entry> m_entries;
    bool m_has_error = false;

    void push(Level *level);
    void pop();
    void error();

    virtual void null() override;
    virtual void boolean(bool b) override;
    virtual void integer(int64_t i) override;
    virtual void number(double n) override;
    virtual void string(const char *s, size_t len) override;
    virtual void map_start() override;
    virtual void map_key(const char *s, size_t len) override;
    virtual void map_end() override;
    virtual void array_start() override;
    virtual void array_end() override;
  };

  Entry* m_entries = nullptr;

  friend class MetricDataSum;
  friend class MetricHistory;
};

//
// MetricDataSum
//

class MetricDataSum {
public:
  ~MetricDataSum();

  void sum(MetricData &data, bool initial);
  void serialize(Data &out, bool initial);
  void serialize(Data::Builder &db, bool initial);

private:

  //
  // MetricDataSum::Node
  //

  struct Node : public List<Node>::Item {
    pjs::Ref<pjs::Str> key;
    std::map<pjs::Str*, Node*> submap;
    List<Node> subs;
    bool serialized = false;
    double values[1];
    static auto make(int dimensions) -> Node*;
    ~Node();
  private:
    Node() {}
  };

  //
  // MetricDataSum::Entry
  //

  struct Entry : public List<Entry>::Item {
    pjs::Ref<pjs::Str> name;
    pjs::Ref<pjs::Str> type;
    pjs::Ref<pjs::Str> shape;
    int dimensions;
    std::unique_ptr<Node> root;
  };

  List<Entry> m_entries;
  std::unordered_map<pjs::Str*, Entry*> m_entry_map;
};

//
// MetricHistory
//

class MetricHistory {
public:
  MetricHistory(size_t duration = 1) : m_duration(duration) {}
  ~MetricHistory();

  void update(MetricData &data);
  void step();
  void serialize(Data::Builder &db);
  void serialize(Data::Builder &db, const std::string &metric_name);

private:

  //
  // MetricHistory::Node
  //

  struct Node {
    pjs::Ref<pjs::Str> key;
    std::map<pjs::Str*, Node*> submap;
    double values[1];
    static auto make(int dimensions, int duration) -> Node*;
    ~Node();
  private:
    Node() {}
  };

  //
  // MetricHistory::Entry
  //

  struct Entry {
    pjs::Ref<pjs::Str> name;
    pjs::Ref<pjs::Str> type;
    pjs::Ref<pjs::Str> shape;
    int dimensions;
    std::unique_ptr<Node> root;
  };

  size_t m_duration;
  size_t m_current = 0;
  size_t m_start = 0;
  std::unordered_map<pjs::Str*, Entry*> m_entries;

  void serialize(Data::Builder &db, Entry *entry, Node *node, int level, bool recursive);
};

//
// MetricTemplate
//

template<class T>
class MetricTemplate : public pjs::ObjectTemplate<T, Metric> {
public:
  auto with_labels(pjs::Str *const *labels, int count) -> T* {
    return static_cast<T*>(Metric::with_labels(labels, count));
  }

protected:
  using pjs::ObjectTemplate<T, Metric>::ObjectTemplate;

private:
  virtual auto create_new(Metric *parent, pjs::Str **labels) -> Metric* override {
    return T::make(parent, labels);
  }
};

//
// Counter
//

class Counter : public MetricTemplate<Counter> {
public:
  virtual void zero() override;

  void increase(double n = 1);
  auto value() const -> double { return m_value; }

private:
  Counter(pjs::Str *name, pjs::Array *label_names, MetricSet *set = nullptr);
  Counter(Metric *parent, pjs::Str **labels);

  virtual void value_of(pjs::Value &out) override {
    out.set(m_value);
  }

  virtual auto get_type() -> pjs::Str* override;

  virtual auto get_value(int dim) -> double override {
    return m_value;
  }

  virtual void set_value(int dim, double value) override {
    m_value = value;
    create_value();
  }

  virtual void dump(const std::function<void(pjs::Str*, double)> &out) override {
    out(nullptr, m_value);
  };

  double m_value = 0;

  friend class pjs::ObjectTemplate<Counter, Metric>;
};

//
// Gauge
//

class Gauge : public MetricTemplate<Gauge> {
public:
  virtual void zero() override;

  void set(double n);
  void increase(double n = 1);
  void decrease(double n = 1);
  auto value() const -> double { return m_value; }

private:
  Gauge(pjs::Str *name, pjs::Array *label_names, const std::function<void(Gauge*)> &on_collect = nullptr, MetricSet *set = nullptr);
  Gauge(Metric *parent, pjs::Str **labels);

  virtual void value_of(pjs::Value &out) override {
    out.set(m_value);
  }

  virtual auto get_type() -> pjs::Str* override;

  virtual auto get_value(int dim) -> double override {
    return m_value;
  }

  virtual void set_value(int dim, double value) override {
    m_value = value;
    create_value();
  }

  virtual void dump(const std::function<void(pjs::Str*, double)> &out) override {
    out(nullptr, m_value);
  };

  virtual void collect() override {
    if (m_on_collect) {
      m_on_collect(this);
    }
  }

  double m_value = 0;
  std::function<void(Gauge*)> m_on_collect;

  friend class pjs::ObjectTemplate<Gauge, Metric>;
};

//
// Histogram
//

class Histogram : public MetricTemplate<Histogram> {
public:
  virtual void zero() override;

  void observe(double n);

private:
  Histogram(pjs::Str *name, pjs::Array *buckets, pjs::Array *label_names, MetricSet *set = nullptr);
  Histogram(Metric *parent, pjs::Str **labels);

  virtual void value_of(pjs::Value &out) override;
  virtual auto get_type() -> pjs::Str* override;
  virtual auto get_dim() -> int override;
  virtual auto get_value(int dim) -> double override;
  virtual void set_value(int dim, double value) override;
  virtual void dump(const std::function<void(pjs::Str*, double)> &out) override;

  pjs::Ref<Histogram> m_root;
  pjs::Ref<pjs::Array> m_buckets;
  pjs::Ref<algo::Percentile> m_percentile;
  std::vector<pjs::Ref<pjs::Str>> m_labels;
  double m_sum = 0;
  size_t m_count = 0;

  friend class pjs::ObjectTemplate<Histogram, Metric>;
};

//
// Stats
//

class Stats : public pjs::ObjectTemplate<Stats>
{
};

} // namespace stats
} // namespace pipy

#endif // STATS_HPP
