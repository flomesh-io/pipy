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
  struct Options {
    int size = 0;
    double ttl = 0;
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
// HashingLoadBalancer
//

class HashingLoadBalancer : public pjs::ObjectTemplate<HashingLoadBalancer> {
public:
  void add(pjs::Str *target);
  auto select(const pjs::Value &key) -> pjs::Str*;
  void deselect(pjs::Str *target) {}

private:
  HashingLoadBalancer(pjs::Array *targets, Cache *unhealthy = nullptr);
  ~HashingLoadBalancer();

  std::vector<pjs::Ref<pjs::Str>> m_targets;
  pjs::Ref<Cache> m_unhealthy;

  friend class pjs::ObjectTemplate<HashingLoadBalancer>;
};

//
// RoundRobinLoadBalancer
//

class RoundRobinLoadBalancer : public pjs::ObjectTemplate<RoundRobinLoadBalancer> {
public:
  void set(pjs::Str *target, int weight);
  auto select() -> pjs::Str*;
  void deselect(pjs::Str *target) {}

private:
  RoundRobinLoadBalancer(pjs::Object *targets, Cache *unhealthy = nullptr);
  ~RoundRobinLoadBalancer();

  struct Target {
    int weight;
    int hits;
    double usage;
  };

  std::map<pjs::Ref<pjs::Str>, Target> m_targets;
  pjs::Ref<Cache> m_unhealthy;
  int m_total_weight = 0;
  int m_total_hits = 0;

  friend class pjs::ObjectTemplate<RoundRobinLoadBalancer>;
};

//
// LeastWorkLoadBalancer
//

class LeastWorkLoadBalancer : public pjs::ObjectTemplate<LeastWorkLoadBalancer> {
public:
  void set(pjs::Str *target, double weight);
  auto select() -> pjs::Str*;
  void deselect(pjs::Str *target);

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

  friend class pjs::ObjectTemplate<LeastWorkLoadBalancer>;
};

//
// ResourcePool
//

class ResourcePool : public pjs::ObjectTemplate<ResourcePool> {
public:
  void allocate(pjs::Context &ctx, const pjs::Value &pool, const pjs::Value &tenant, pjs::Value &resource);
  void free(const pjs::Value &resource);
  void free_tenant(const pjs::Value &tenant);

private:
  ResourcePool(pjs::Function *allocator)
    : m_allocator(allocator) {}

  typedef std::list<pjs::Value> Pool;
  typedef std::map<pjs::Value, pjs::Value> Tenant;

  struct Allocated {
    pjs::Value pool;
    pjs::Value tenant;
  };

  pjs::Ref<pjs::Function> m_allocator;
  std::map<pjs::Value, Pool> m_pools;
  std::map<pjs::Value, Allocated> m_allocated;
  std::map<pjs::Value, Tenant> m_tenants;

  friend class pjs::ObjectTemplate<ResourcePool>;
};

//
// Percentile
//

class Percentile : public pjs::ObjectTemplate<Percentile> {
public:
  void reset();
  void set(int bucket, size_t count);
  void observe(double sample);
  auto calculate(int percentage) -> double;
  void dump(const std::function<void(double, double)> &cb);

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