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
#include "data.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace pipy {
namespace stats {

class MetricSet;

//
// Metric
//

class Metric : public pjs::ObjectTemplate<Metric> {
public:
  static auto local() -> MetricSet&;

  auto name() const -> pjs::Str* { return m_name; }
  auto label() const -> pjs::Str* { return m_label; }
  auto with_labels(pjs::Str *const *labels, int count) -> Metric*;
  void clear();

protected:
  Metric(pjs::Str *name, pjs::Array *label_names, MetricSet *set = nullptr);
  Metric(Metric *parent, pjs::Str **labels);
  virtual ~Metric() {}

  void create_value();

  virtual auto create_new(Metric *parent, pjs::Str **labels) -> Metric* = 0;
  virtual void collect() {}
  virtual void dump(const std::function<void(pjs::Str*, pjs::Str*, double)> &out) {};

private:
  auto get_sub(pjs::Str **labels) -> Metric*;

  void dump_tree(
    pjs::Str **label_names,
    pjs::Str **label_values,
    const std::function<void(int, double)> &out
  );

  pjs::Ref<pjs::Str> m_name;
  pjs::Ref<pjs::Str> m_label;
  int m_label_index;
  bool m_has_value = false;
  bool m_has_serialized = false;
  std::shared_ptr<std::vector<pjs::Ref<pjs::Str>>> m_label_names;
  std::unordered_map<pjs::Ref<pjs::Str>, pjs::Ref<Metric>> m_subs;

  static std::unordered_map<pjs::Ref<pjs::Str>, pjs::Ref<Metric>> s_all_metrics;

  friend class pjs::ObjectTemplate<Metric>;
  friend class MetricSet;
};

//
// MetricSet
//

class MetricSet {
public:
  auto get(pjs::Str *name) -> Metric*;
  void collect_all();
  void serialize_init(Data &out);
  void serialize_update(Data &out);
  void deserialize(Data &in);
  void to_prometheus(Data &out);

private:
  std::vector<pjs::Ref<Metric>> m_metrics;
  std::unordered_map<pjs::Ref<pjs::Str>, Metric*> m_metric_map;

  void add(Metric *metric);

  friend class Metric;
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
  void zero();
  void increase(double n = 1);
  auto value() const -> double { return m_value; }

private:
  Counter(pjs::Str *name, pjs::Array *label_names);
  Counter(Metric *parent, pjs::Str **labels);

  virtual void value_of(pjs::Value &out) override {
    out.set(m_value);
  }

  virtual void dump(const std::function<void(pjs::Str*, pjs::Str*, double)> &out) override {
    out(nullptr, nullptr, m_value);
  };

  double m_value = 0;

  friend class pjs::ObjectTemplate<Counter, Metric>;
};

//
// Gauge
//

class Gauge : public MetricTemplate<Gauge> {
public:
  void zero();
  void set(double n);
  void increase(double n = 1);
  void decrease(double n = 1);
  auto value() const -> double { return m_value; }

private:
  Gauge(pjs::Str *name, pjs::Array *label_names, const std::function<void(Gauge*)> &on_collect = nullptr);
  Gauge(Metric *parent, pjs::Str **labels);

  virtual void value_of(pjs::Value &out) override {
    out.set(m_value);
  }

  virtual void dump(const std::function<void(pjs::Str*, pjs::Str*, double)> &out) override {
    out(nullptr, 0, m_value);
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
  void zero();
  void observe(double n);

private:
  Histogram(pjs::Str *name, pjs::Array *buckets, pjs::Array *label_names);
  Histogram(Metric *parent, pjs::Str **labels);

  virtual void value_of(pjs::Value &out) override;
  virtual void dump(const std::function<void(pjs::Str*, pjs::Str*, double)> &out) override;

  pjs::Ref<Histogram> m_root;
  pjs::Ref<pjs::Array> m_buckets;
  pjs::Ref<algo::Percentile> m_percentile;
  std::vector<pjs::Ref<pjs::Str>> m_labels;

  friend class pjs::ObjectTemplate<Histogram, Metric>;
};

//
// Stats
//

class Stats : public pjs::ObjectTemplate<Stats>
{
};

//
// MetricClient
//

class MetricClient {
public:

private:
  std::vector<Metric*> m_metrics;
};

//
// MetricServer
//

class MetricServer {
};

} // namespace stats
} // namespace pipy

#endif // STATS_HPP
