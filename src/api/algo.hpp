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

#ifndef ALGO_HPP
#define ALGO_HPP

#include "pjs/pjs.hpp"
#include "list.hpp"
#include "net.hpp"
#include "timer.hpp"
#include "options.hpp"

#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

namespace pipy {
namespace algo {

//
// Cache
//

class Cache : public pjs::ObjectTemplate<Cache> {
public:
  struct Options : public pipy::Options {
    int size = 0;
    double ttl = 0;

    Options() {}
    Options(pjs::Object *options);
  };

  bool get(pjs::Context &ctx, const pjs::Value &key, pjs::Value &value);
  void set(pjs::Context &ctx, const pjs::Value &key, const pjs::Value &value);
  bool get(const pjs::Value &key, pjs::Value &value);
  void set(const pjs::Value &key, const pjs::Value &value);
  bool has(const pjs::Value &key);
  bool find(const pjs::Value &key, pjs::Value &value);
  bool remove(const pjs::Value &key);
  bool remove(pjs::Context &ctx, const pjs::Value &key);
  bool clear(pjs::Context &ctx);

private:
  Cache(const Options &options, pjs::Function *allocate = nullptr, pjs::Function *free = nullptr);
  ~Cache();

  struct Entry {
    pjs::Value value;
    double ttl;
  };

  Options m_options;
  pjs::Ref<pjs::Function> m_allocate;
  pjs::Ref<pjs::Function> m_free;
  pjs::Ref<pjs::OrderedHash<pjs::Value, Entry>> m_cache;

  bool get(
    const pjs::Value &key, pjs::Value &value,
    const std::function<bool(pjs::Value &)> &allocate
  );

  void set(
    const pjs::Value &key, const pjs::Value &value,
    const std::function<bool(const pjs::Value &, const pjs::Value &)> &free
  );

  friend class pjs::ObjectTemplate<Cache>;
};

//
// Quota
//

class Quota : public pjs::ObjectTemplate<Quota> {
public:
  struct Options : public pipy::Options {
    pjs::Ref<pjs::Str> key;
    double max = std::numeric_limits<double>::infinity();
    double per = 0;
    double produce = 0;
    Options() {}
    Options(pjs::Object *options);
  };

  //
  // Quota::Counter
  //

  class Counter : public pjs::RefCountMT<Counter> {
  public:
    static auto get(
      const std::string &key,
      double initial_value,
      double maximum_value,
      double produce_value,
      double produce_cycle
    ) -> Counter*;

    void init(
      double initial_value,
      double maximum_value,
      double produce_value,
      double produce_cycle
    );

    auto initial() const -> double { return m_initial_value; }
    auto current() const -> double { return m_current_value.load(); }
    void produce(double value);
    auto consume(double value) -> double;
    void enqueue(Quota *quota);
    void dequeue(Quota *quota);

  private:
    Counter(
      const std::string &key,
      double initial_value,
      double maximum_value,
      double produce_value,
      double produce_cycle
    );
    ~Counter();

    Net& m_net;
    std::string m_key;
    std::atomic<double> m_initial_value;
    std::atomic<double> m_maximum_value;
    std::atomic<double> m_produce_value;
    std::atomic<double> m_produce_cycle;
    std::atomic<double> m_current_value;
    std::atomic<bool> m_is_producing_scheduled;
    std::set<Quota*> m_quotas;
    std::mutex m_quotas_mutex;
    Timer m_timer;

    void schedule_producing();
    void on_produce();
    void finalize();

    static std::map<std::string, Counter*> m_counter_map;
    static std::mutex m_counter_map_mutex;

    friend class pjs::RefCountMT<Counter>;
  };

  //
  // Quota::Consumer
  //

  class Consumer : public List<Consumer>::Item {
  public:
    virtual bool on_consume(Quota *quota) = 0;

  protected:
    ~Consumer() {
      if (m_quota) {
        m_quota->dequeue(this);
      }
    }

    pjs::Ref<Quota> m_quota;

    friend class Quota;
  };

  void reset();
  auto initial() const -> double { return m_counter ? m_counter->initial() : m_initial_value; }
  auto current() const -> double { return m_counter ? m_counter->current() : m_current_value; }
  void produce(double value);
  void produce_async(double value);
  auto consume(double value) -> double;
  void enqueue(Consumer *consumer);
  void dequeue(Consumer *consumer);

private:
  Quota(double initial_value, const Options &options);
  ~Quota();

  Options m_options;
  Net& m_net;
  pjs::Ref<Counter> m_counter;
  double m_initial_value;
  double m_current_value;
  bool m_is_producing_scheduled = false;
  List<Consumer> m_consumers;
  Timer m_timer;

  void schedule_producing();
  void on_produce();
  void on_produce_async();

  friend class pjs::ObjectTemplate<Quota>;
};

//
// SharedMap
//

class SharedMap : public pjs::ObjectTemplate<SharedMap> {
public:
  SharedMap(pjs::Str *name);

  auto size() -> size_t;
  void clear();
  bool erase(pjs::Str *key);
  bool has(pjs::Str *key);
  bool get(pjs::Str *key, pjs::Value &value);
  void set(pjs::Str *key, const pjs::Value &value);
  auto add(pjs::Str *key, double value) -> double;
  auto sub(pjs::Str *key, double value) -> double;

private:

  //
  // SharedMap::Map
  //

  class Map : public pjs::RefCountMT<Map> {
  public:
    static auto get(const std::string &name) -> Map*;

    auto size() -> size_t;
    void clear();
    bool erase(pjs::Str::CharData *key);
    bool has(pjs::Str::CharData *key);
    bool get(pjs::Str::CharData *key, pjs::SharedValue &value);
    void set(pjs::Str::CharData *key, const pjs::SharedValue &value);
    auto add(pjs::Str::CharData *key, double value) -> double;
    auto sub(pjs::Str::CharData *key, double value) -> double;

  private:
    typedef pjs::Ref<pjs::Str::CharData> Key;

    struct Hash {
      size_t operator()(const Key &k) const {
        std::hash<std::string> h;
        return h(k->str());
      }
    };

    struct EqualTo {
      bool operator()(const Key &a, const Key &b) const {
        return a->str() == b->str();
      }
    };

    std::unordered_map<Key, pjs::SharedValue, Hash, EqualTo> m_map;
    std::mutex m_mutex;

    static std::map<std::string, Map*> m_maps;
    static std::mutex m_maps_mutex;
  };

  pjs::Ref<Map> m_map;
};

//
// ResourcePool
//

class ResourcePool : public pjs::ObjectTemplate<ResourcePool> {
public:
  void allocate(pjs::Context &ctx, const pjs::Value &tag, pjs::Value &resource);
  void free(const pjs::Value &resource);

private:
  ResourcePool(pjs::Function *allocator)
    : m_allocator(allocator) {}

  typedef std::list<pjs::Value> Pool;

  struct Allocated {
    pjs::Value tag;
  };

  pjs::Ref<pjs::Function> m_allocator;
  std::map<pjs::Value, Pool> m_pools;
  std::map<pjs::Value, Allocated> m_allocated;

  friend class pjs::ObjectTemplate<ResourcePool>;
};

//
// URLRouter
//

class URLRouter : public pjs::ObjectTemplate<URLRouter> {
public:
  void add(const std::string &url, const pjs::Value &value);
  bool find(const std::string &url, pjs::Value &value);

private:
  URLRouter();
  URLRouter(pjs::Object *rules);
  ~URLRouter();

  struct Node {
    std::map<std::string, Node*> children;
    pjs::Value value;

    auto child(const std::string &name) -> Node* {
      auto i = children.find(name);
      return i == children.end() ? nullptr : i->second;
    }

    auto new_child(const std::string &name) -> Node* {
      auto i = children.find(name);
      if (i != children.end()) return i->second;
      return children[name] = new Node;
    }

    ~Node() {
      for (auto &i : children) {
        delete i.second;
      }
    }
  };

  Node* m_root;

  void dump(Node *node, int level);

  friend class pjs::ObjectTemplate<URLRouter>;
};

//
// LoadBalancer
//

class LoadBalancer : public pjs::ObjectTemplate<LoadBalancer> {
public:
  class Resource;

private:
  //
  // LoadBalancer::Pool
  //

  class Pool : public pjs::RefCount<Pool>, public List<Pool>::Item {
  public:
    Pool(LoadBalancer *l, const pjs::Value &k, const pjs::Value &t)
      : lb(l), key(k), target(t) {}

    LoadBalancer* lb;
    pjs::Value key;
    pjs::Value target;
    int capacity = 0;
    double weight = 1;
    double step = 0;
    double load = 0;

    auto allocate() -> Resource*;

  private:
    List<Resource> m_resources;

    friend class Resource;
  };

public:

  //
  // LoadBalancer::Algorithm
  //

  enum Algorithm {
    ROUND_ROBIN,
    LEAST_LOAD,
  };

  //
  // LoadBalancer::Options
  //

  struct Options : public pipy::Options {
    Algorithm algorithm = ROUND_ROBIN;
    pjs::Ref<pjs::Function> key_f;
    pjs::Ref<pjs::Function> weight_f;
    pjs::Ref<pjs::Function> capacity_f;
    int capacity = 0;
    Options() {}
    Options(pjs::Object *options);
  };

  //
  // LoadBalancer::Resource
  //

  class Resource :
    public pjs::ObjectTemplate<Resource>,
    public List<Resource>::Item
  {
  public:
    auto target() const -> const pjs::Value & { return m_target; }
    void free();

  private:
    Resource(Pool *pool, const pjs::Value &target)
      : m_pool(pool), m_target(target) {}

    ~Resource();

    pjs::Ref<Pool> m_pool;
    pjs::Value m_target;
    int m_load = 0;

    void increase_load();

    friend class pjs::ObjectTemplate<Resource>;
    friend class Pool;
  };

  void provision(pjs::Context &ctx, pjs::Array *targets);
  auto schedule(pjs::Context &ctx, int size, pjs::Function *validator = nullptr) -> pjs::Array*;
  auto allocate(pjs::Context &ctx, const pjs::Value &tag = pjs::Value::undefined, pjs::Function *validator = nullptr) -> Resource*;

private:
  LoadBalancer(const Options &options)
    : m_options(options) {}

  ~LoadBalancer();

  Options m_options;
  std::map<pjs::Value, Pool*> m_targets;
  std::vector<pjs::Ref<Pool>> m_pools;
  List<Pool> m_queue;

  auto next(const std::function<bool(const pjs::Value &)> &validator) -> Pool*;
  void increase_load(Pool *pool);
  void decrease_load(Pool *pool);
  void sort_forward(List<Pool> &queue, Pool *pool);
  void sort_backward(List<Pool> &queue, Pool *pool);

  friend class pjs::ObjectTemplate<LoadBalancer>;
};

//
// Percentile
//

class Percentile : public pjs::ObjectTemplate<Percentile> {
public:
  void reset();
  auto size() const -> size_t { return m_buckets.size(); }
  auto get(int bucket) -> size_t;
  void set(int bucket, size_t count);
  void observe(double sample);
  auto calculate(int percentage) -> double;
  void dump(const std::function<void(double, size_t)> &cb);

private:
  Percentile(pjs::Array *buckets);

  std::vector<size_t> m_counts;
  std::vector<double> m_buckets;
  size_t m_sample_count;

  friend class pjs::ObjectTemplate<Percentile>;
};

//
// Algo
//

class Algo : public pjs::ObjectTemplate<Algo> {
public:
  static auto hash(const pjs::Value &value) -> size_t;
};

} // namespace algo
} // namespace pipy

#endif // ALGO_HPP
