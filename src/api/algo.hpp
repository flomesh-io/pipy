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
#include "timer.hpp"
#include "options.hpp"

#include <map>
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
  bool find(const pjs::Value &key, pjs::Value &value);
  bool remove(pjs::Context &ctx, const pjs::Value &key);
  bool clear(pjs::Context &ctx);

private:
  Cache(const Options &options, pjs::Function *allocate, pjs::Function *free = nullptr);
  ~Cache();

  struct Entry {
    pjs::Value value;
    double ttl;
  };

  Options m_options;
  pjs::Ref<pjs::Function> m_allocate;
  pjs::Ref<pjs::Function> m_free;
  pjs::Ref<pjs::OrderedHash<pjs::Value, Entry>> m_cache;

  friend class pjs::ObjectTemplate<Cache>;
};

//
// Quota
//

class Quota : public pjs::ObjectTemplate<Quota> {
public:
  struct Options : public pipy::Options {
    double per = 0;
    double produce = 0;
    Options() {}
    Options(pjs::Object *options);
  };

  //
  // Quota::Consumer
  //

  class Consumer : public List<Consumer>::Item {
  public:
    virtual void on_consume(Quota *quota) = 0;

  protected:
    Consumer() {}
    ~Consumer() { set_quota(nullptr); }

    void set_quota(Quota *quota);

    pjs::Ref<Quota> m_quota;

    friend class Quota;
  };

  void reset();
  auto initial() const -> double { return m_initial_value; }
  auto current() const -> double { return m_current_value; }
  void produce(double value);
  auto consume(double value) -> double;
  void enqueue(Consumer *consumer) { consumer->set_quota(this); }
  void dequeue(Consumer *consumer) { consumer->set_quota(nullptr); }

private:
  Quota(double initial_value, const Options &options);

  Options m_options;
  double m_initial_value;
  double m_current_value;
  bool m_is_producing_scheduled = false;
  List<Consumer> m_consumers;
  Timer m_timer;

  void schedule_producing();

  friend class pjs::ObjectTemplate<Quota>;
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

  //
  // LoadBalancer::Resource
  //

  class Resource :
    public pjs::ObjectTemplate<Resource>,
    public List<Resource>::Item
  {
  public:
    auto id() -> pjs::Str* { return m_id; }

  private:
    Resource(pjs::Str *id) : m_id(id) {}

    pjs::Ref<pjs::Str> m_id;

    friend class pjs::ObjectTemplate<Resource>;
  };

  auto next(pjs::Object *session_key, const pjs::Value &target_key = pjs::Value::undefined) -> Resource*;

  virtual auto select(const pjs::Value &key) -> pjs::Str* = 0;
  virtual void deselect(pjs::Str *id) = 0;

protected:
  ~LoadBalancer();

private:

  //
  // LoadBalancer::Session
  //

  class Session :
    public pjs::Pooled<Session>,
    public pjs::Object::WeakPtr::Watcher
  {
  public:
    Session(LoadBalancer *lb, pjs::Object *key);

    auto key() const -> const pjs::WeakRef<pjs::Object>& { return m_key; }
    auto resource() const -> Resource* { return m_resource; }
    void resource(Resource *res) { m_resource = res; }

  private:
    virtual void on_weak_ptr_gone() override;

    LoadBalancer* m_lb;
    pjs::WeakRef<pjs::Object> m_key;
    pjs::Ref<Resource> m_resource;
  };

  //
  // LoadBalancer::Target
  //

  struct Target : public pjs::Pooled<Target> {
    List<Resource> resources;
  };

  std::map<pjs::WeakRef<pjs::Object>, Session*> m_sessions;
  std::map<pjs::Ref<pjs::Str>, Target*> m_targets;

  void close_session(Session *session);

  friend class pjs::ObjectTemplate<LoadBalancer>;
};

//
// HashingLoadBalancer
//

class HashingLoadBalancer : public pjs::ObjectTemplate<HashingLoadBalancer, LoadBalancer> {
public:
  void add(pjs::Str *target);

  virtual auto select(const pjs::Value &key) -> pjs::Str* override;
  virtual void deselect(pjs::Str *target) override {}

private:
  HashingLoadBalancer(pjs::Array *targets, Cache *unhealthy = nullptr);
  ~HashingLoadBalancer();

  std::vector<pjs::Ref<pjs::Str>> m_targets;
  pjs::Ref<Cache> m_unhealthy;

  friend class pjs::ObjectTemplate<HashingLoadBalancer, LoadBalancer>;
};

//
// RoundRobinLoadBalancer
//

class RoundRobinLoadBalancer : public pjs::ObjectTemplate<RoundRobinLoadBalancer, LoadBalancer> {
public:
  void set(pjs::Str *target, int weight);

  virtual auto select(const pjs::Value &key) -> pjs::Str* override;
  virtual void deselect(pjs::Str *target) override {}

private:
  RoundRobinLoadBalancer(pjs::Object *targets, Cache *unhealthy = nullptr);
  ~RoundRobinLoadBalancer();

  struct Target {
    pjs::Ref<pjs::Str> id;
    int weight;
    int hits;
    double usage;
  };

  std::list<Target> m_targets;
  std::map<pjs::Str*, Target*> m_target_map;
  pjs::Ref<Cache> m_unhealthy;
  int m_total_weight = 0;
  int m_total_hits = 0;

  friend class pjs::ObjectTemplate<RoundRobinLoadBalancer, LoadBalancer>;
};

//
// LeastWorkLoadBalancer
//

class LeastWorkLoadBalancer : public pjs::ObjectTemplate<LeastWorkLoadBalancer, LoadBalancer> {
public:
  void set(pjs::Str *target, double weight);

  virtual auto select(const pjs::Value &key) -> pjs::Str* override;
  virtual void deselect(pjs::Str *target) override;

private:
  LeastWorkLoadBalancer(pjs::Object *targets, Cache *unhealthy = nullptr);
  ~LeastWorkLoadBalancer();

  struct Target {
    int weight;
    int hits;
    double usage;
  };

  std::map<pjs::Ref<pjs::Str>, Target> m_targets;
  pjs::Ref<Cache> m_unhealthy;

  friend class pjs::ObjectTemplate<LeastWorkLoadBalancer, LoadBalancer>;
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
