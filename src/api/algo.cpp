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

#include "algo.hpp"
#include "utils.hpp"

namespace pipy {
namespace algo {

Cache::Cache(pjs::Function *allocate, pjs::Function *free)
  : m_allocate(allocate)
  , m_free(free)
{
}

Cache::~Cache() {
}

bool Cache::get(pjs::Context &ctx, const pjs::Value &key, pjs::Value &value) {
  auto i = m_cache.find(key);
  if (i != m_cache.end()) {
    value = i->second.value;
    return true;
  }

  if (!m_allocate) return false;

  pjs::Value arg(key);
  (*m_allocate)(ctx, 1, &arg, value);
  if (!ctx.ok()) return false;

  m_cache[key].value = value;
  return true;
}

bool Cache::clear(pjs::Context &ctx) {
  if (m_free) {
    for (auto &p : m_cache) {
      pjs::Value argv[2], ret;
      argv[0] = p.first;
      argv[1] = p.second.value;
      (*m_free)(ctx, 2, argv, ret);
      if (!ctx.ok()) return false;
    }
  }
  m_cache.clear();
  return true;
}

//
// URLRouter
//

URLRouter::URLRouter()
  : m_root(new Node())
{
}

URLRouter::URLRouter(pjs::Object *rules)
  : URLRouter()
{
  if (rules) {
    rules->iterate_all(
      [this](pjs::Str *k, pjs::Value &v) {
        add(k->str(), v);
      }
    );
  }
}

URLRouter::~URLRouter() {
  delete m_root;
}

void URLRouter::add(const std::string &url, const pjs::Value &value) {
  auto segs = utils::split(url, '/');
  auto domain = segs.front();
  segs.pop_front();

  if (segs.empty() || domain.find_first_of(':') != std::string::npos) {
    throw std::runtime_error("invalid URL pattern");
  }

  auto node = m_root;

  if (!domain.empty()) {
    auto segs = utils::split(domain, '.');
    for (const auto &seg : segs) {
      node = node->new_child(seg);
    }
  }

  node = node->new_child("/");

  if (segs.back() == "*") {
    for (const auto &seg : segs) {
      if (!seg.empty()) {
        node = node->new_child(seg);
      }
    }
    node->value = value;
  } else {
    auto i = url.find_first_of('/');
    node->new_child(url.substr(i))->value = value;
  }
}

bool URLRouter::find(const std::string &url, pjs::Value &value) {
  static std::string s_slash("/");
  static std::string s_asterisk("*");

  auto path_start = url.find_first_of('/');
  if (path_start == std::string::npos) return false;

  auto path_end = url.find_first_of('?', path_start);
  if (path_end == std::string::npos) path_end = url.length();

  auto domain_end = url.find_last_of(':', path_start);
  if (domain_end == std::string::npos) domain_end = path_start;

  std::function<Node*(Node*, int)> find_host, find_path;

  find_host = [&](Node *node, int p) -> Node* {
    auto i = p;
    while (i < domain_end && url[i] != '.') i++;
    auto s = url.substr(p, i - p);
    node = node->child(s);
    if (!node) return nullptr;
    p = i + 1;
    if (p <= domain_end) return find_host(node, p);
    node = node->child(s_slash);
    if (!node) return nullptr;
    if (auto exact = node->child(url.substr(path_start, path_end - path_start))) {
      return exact;
    }
    return find_path(node, path_start + 1);
  };

  find_path = [&](Node *node, int p) -> Node* {
    auto i = p;
    while (i < path_end && url[i] != '/') i++;
    auto s = url.substr(p, i - p);
    if (auto c = node->child(s)) {
      p = i + 1;
      c = p >= path_end ? c->child(s_asterisk) : find_path(c, p);
      if (c) return c;
    }
    if (auto c = node->child(s_asterisk)) return c;
    return nullptr;
  };

  auto node = find_host(m_root, 0);
  if (!node) {
    auto i = 0;
    while (i < domain_end && url[i] != '.') i++;
    if (i < domain_end) {
      if (auto c = m_root->child(s_asterisk)) {
        node = find_host(c, i + 1);
      }
    }
  }
  if (!node) {
    if (auto c = m_root->child(s_slash)) {
      if (auto exact = c->child(url.substr(path_start, path_end - path_start))) {
        node = exact;
      } else {
        node = find_path(c, path_start + 1);
      }
    }
  }

  if (node) {
    value = node->value;
    return true;
  }

  return false;
}

void URLRouter::dump(Node *node, int level) {
  for (auto &i : node->children) {
    std::cout << std::string(level * 2, ' ');
    std::cout << i.first << std::endl;
    dump(i.second, level + 1);
  }
}

//
// HashingLoadBalancer
//

HashingLoadBalancer::HashingLoadBalancer()
{
}

HashingLoadBalancer::HashingLoadBalancer(pjs::Array *targets)
  : m_targets(targets->length())
{
  targets->iterate_all(
    [this](pjs::Value &v, int i) {
      auto s = v.to_string();
      m_targets[i] = s;
      s->release();
    }
  );
}

HashingLoadBalancer::~HashingLoadBalancer()
{
}

void HashingLoadBalancer::add(pjs::Str *target) {
  m_targets.push_back(target);
}

auto HashingLoadBalancer::select(const pjs::Value &tag) -> pjs::Str* {
  if (m_targets.empty()) return nullptr;
  std::hash<pjs::Value> hash;
  auto h = hash(tag);
  return m_targets[h % m_targets.size()];
}

//
// RoundRobinLoadBalancer
//

RoundRobinLoadBalancer::RoundRobinLoadBalancer()
{
}

RoundRobinLoadBalancer::RoundRobinLoadBalancer(pjs::Object *rules)
  : RoundRobinLoadBalancer()
{
  if (rules) {
    rules->iterate_all(
      [this](pjs::Str *k, pjs::Value &v) {
        set(k, v.to_number());
      }
    );
  }
}

RoundRobinLoadBalancer::~RoundRobinLoadBalancer()
{
}

void RoundRobinLoadBalancer::set(pjs::Str *target, int weight) {
  if (weight < 0) weight = 0;

  auto hits = 0;
  if (m_total_weight > 0) {
    hits = (m_total_hits * weight) / m_total_weight;
  }

  auto i = m_targets.find(target);
  if (i == m_targets.end()) {
    auto &t = m_targets[target];
    t.weight = weight;
    t.hits = hits;
    m_total_weight += weight;
    m_total_hits += hits;
  } else {
    auto &t = i->second;
    m_total_weight += weight - t.weight;
    m_total_hits += hits - t.hits;
    t.weight = weight;
    t.hits = hits;
  }

  if (m_total_weight > 0) {
    for (auto &i : m_targets) {
      auto &t = i.second;
      if (t.weight > 0) {
        t.usage = double(t.hits) / m_total_weight;
      }
    }
  }
}

auto RoundRobinLoadBalancer::select(const pjs::Value &tag) -> pjs::Str* {
  if (!tag.is_undefined()) {
    auto i = m_cache.find(tag);
    if (i != m_cache.end()) return i->second;
  }

  double min = 0;
  std::map<pjs::Ref<pjs::Str>, Target>::value_type *p = nullptr;
  for (auto &i : m_targets) {
    auto &t = i.second;
    if (t.weight > 0) {
      if (!p || t.usage < min) {
        min = t.usage;
        p = &i;
      }
    }
  }

  if (!p) return nullptr;

  auto &t = p->second;
  t.hits++;
  t.usage = double(t.hits) / t.weight;

  m_total_hits++;
  if (m_total_hits == m_total_weight) {
    for (auto &i : m_targets) {
      auto &t = i.second;
      t.hits = 0;
      t.usage = 0;
    }
    m_total_hits = 0;
  }

  if (!tag.is_undefined()) {
    m_cache[tag] = p->first;
  }

  return p->first;
}

//
// LeastWorkLoadBalancer
//

LeastWorkLoadBalancer::LeastWorkLoadBalancer()
{
}

LeastWorkLoadBalancer::LeastWorkLoadBalancer(pjs::Object *rules)
  : LeastWorkLoadBalancer()
{
  if (rules) {
    rules->iterate_all(
      [this](pjs::Str *k, pjs::Value &v) {
        set(k, v.to_number());
      }
    );
  }
}

LeastWorkLoadBalancer::~LeastWorkLoadBalancer()
{
}

void LeastWorkLoadBalancer::set(pjs::Str *target, double weight) {
  if (weight < 0) weight = 0;

  auto i = m_targets.find(target);
  if (i == m_targets.end()) {
    auto &t = m_targets[target];
    t.weight = weight;
    t.hits = 0;
    t.usage = 0;
  } else {
    auto &t = i->second;
    t.weight = weight;
    t.usage = double(t.hits) / weight;
  }
}

auto LeastWorkLoadBalancer::select(const pjs::Value &tag) -> pjs::Str* {
  if (!tag.is_undefined()) {
    auto i = m_cache.find(tag);
    if (i != m_cache.end()) return i->second;
  }

  double min = 0;
  std::map<pjs::Ref<pjs::Str>, Target>::value_type *p = nullptr;
  for (auto &i : m_targets) {
    auto &t = i.second;
    if (t.weight > 0) {
      if (!p || t.usage < min) {
        min = t.usage;
        p = &i;
      }
    }
  }

  if (!p) return nullptr;

  auto &t = p->second;
  t.hits++;
  t.usage = double(t.hits) / t.weight;

  if (!tag.is_undefined()) {
    m_cache[tag] = p->first;
  }

  return p->first;
}

void LeastWorkLoadBalancer::deselect(pjs::Str *target) {
  auto i = m_targets.find(target);
  if (i != m_targets.end()) {
    auto &t = i->second;
    if (t.hits > 0) {
      t.hits--;
      t.usage = double(t.hits) / t.weight;
    }
  }
}

//
// ResourcePool
//

void ResourcePool::allocate(pjs::Context &ctx, const pjs::Value &pool, const pjs::Value &tenant, pjs::Value &resource) {
  auto &p = m_pools[pool];

  auto alloc = [&]() -> bool {
    if (p.empty()) {
      pjs::Value argv[2];
      argv[0] = pool;
      argv[1] = tenant;
      (*m_allocator)(ctx, 2, argv, resource);
      if (!ctx.ok()) return false;
    } else {
      resource = p.front();
      p.pop_front();
    }
    return true;
  };

  if (tenant.is_undefined()) {
    if (!alloc()) return;
    auto &a = m_allocated[resource];
    a.pool = pool;
    a.tenant = pjs::Value::undefined;

  } else {
    auto &t = m_tenants[tenant];
    resource = t[pool];
    if (!resource.is_undefined()) return;
    if (!alloc()) return;
    t[pool] = resource;
  }

  auto &a = m_allocated[resource];
  a.pool = pool;
  a.tenant = tenant;
}

void ResourcePool::free(const pjs::Value &resource) {
  auto i = m_allocated.find(resource);
  if (i == m_allocated.end()) return;
  auto &a = i->second;
  auto &pool = m_pools[a.pool];
  if (!a.tenant.is_undefined()) {
    auto i = m_tenants.find(a.tenant);
    if (i != m_tenants.end()) {
      i->second.erase(a.pool);
      if (i->second.empty()) m_tenants.erase(i);
    }
  }
  pool.push_back(resource);
  m_allocated.erase(i);
}

void ResourcePool::free_tenant(const pjs::Value &tenant) {
  auto i = m_tenants.find(tenant);
  if (i == m_tenants.end()) return;
  for (auto &t : i->second) {
    auto &p = m_pools[t.first];
    p.push_back(t.second);
    m_allocated.erase(t.second);
  }
  m_tenants.erase(i);
}

} // namespace algo
} // namespace pipy

namespace pjs {

using namespace pipy::algo;

//
// Cache
//

template<> void ClassDef<Cache>::init() {
  ctor([](Context &ctx) -> Object* {
    Function *allocate, *free = nullptr;
    if (!ctx.arguments(1, &allocate, &free)) return nullptr;
    return Cache::make(allocate, free);
  });

  method("get", [](Context &ctx, Object *obj, Value &ret) {
    Value key;
    if (!ctx.arguments(1, &key)) return;
    obj->as<Cache>()->get(ctx, key, ret);
  });

  method("clear", [](Context &ctx, Object *obj, Value &ret) {
    obj->as<Cache>()->clear(ctx);
  });
}

template<> void ClassDef<Constructor<Cache>>::init() {
  super<Function>();
  ctor();
}

//
// URLRouter
//

template<> void ClassDef<URLRouter>::init() {
  ctor([](Context &ctx) -> Object* {
    Object *rules = nullptr;
    if (!ctx.arguments(0, &rules)) return nullptr;
    try {
      return URLRouter::make(rules);
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });

  method("add", [](Context &ctx, Object *obj, Value &ret) {
    std::string url;
    Value value;
    if (!ctx.arguments(2, &url, &value)) return;
    try {
      obj->as<URLRouter>()->add(url, value);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("find", [](Context &ctx, Object *obj, Value &ret) {
    std::string url;
    if (!ctx.arguments(1, &url)) return;
    for (int i = 1; i < ctx.argc(); i++) {
      auto s = ctx.arg(i).to_string();
      url = pipy::utils::path_join(url, s->str());
      s->release();
    }
    obj->as<URLRouter>()->find(url, ret);
  });
}

template<> void ClassDef<Constructor<URLRouter>>::init() {
  super<Function>();
  ctor();
}

//
// HashingLoadBalancer
//

template<> void ClassDef<HashingLoadBalancer>::init() {
  ctor([](Context &ctx) -> Object* {
    Array *targets = nullptr;
    if (!ctx.arguments(0, &targets)) return nullptr;
    return HashingLoadBalancer::make(targets);
  });

  method("add", [](Context &ctx, Object *obj, Value &ret) {
    Str *target;
    if (!ctx.arguments(1, &target)) return;
    obj->as<HashingLoadBalancer>()->add(target);
  });

  method("select", [](Context &ctx, Object *obj, Value &ret) {
    Value tag;
    if (!ctx.arguments(1, &tag)) return;
    if (auto target = obj->as<HashingLoadBalancer>()->select(tag)) {
      ret.set(target);
    }
  });
}

template<> void ClassDef<Constructor<HashingLoadBalancer>>::init() {
  super<Function>();
  ctor();
}

//
// RoundRobinLoadBalancer
//

template<> void ClassDef<RoundRobinLoadBalancer>::init() {
  ctor([](Context &ctx) -> Object* {
    Object *rules = nullptr;
    if (!ctx.arguments(0, &rules)) return nullptr;
    return RoundRobinLoadBalancer::make(rules);
  });

  method("set", [](Context &ctx, Object *obj, Value &ret) {
    Str *target;
    int weight;
    if (!ctx.arguments(2, &target, &weight)) return;
    obj->as<RoundRobinLoadBalancer>()->set(target, weight);
  });

  method("select", [](Context &ctx, Object *obj, Value &ret) {
    Value tag;
    if (!ctx.arguments(0, &tag)) return;
    if (auto target = obj->as<RoundRobinLoadBalancer>()->select(tag)) {
      ret.set(target);
    }
  });
}

template<> void ClassDef<Constructor<RoundRobinLoadBalancer>>::init() {
  super<Function>();
  ctor();
}

//
// LeastWorkLoadBalancer
//

template<> void ClassDef<LeastWorkLoadBalancer>::init() {
  ctor([](Context &ctx) -> Object* {
    Object *rules = nullptr;
    if (!ctx.arguments(0, &rules)) return nullptr;
    return LeastWorkLoadBalancer::make(rules);
  });

  method("set", [](Context &ctx, Object *obj, Value &ret) {
    Str *target;
    int weight;
    if (!ctx.arguments(2, &target, &weight)) return;
    obj->as<LeastWorkLoadBalancer>()->set(target, weight);
  });

  method("select", [](Context &ctx, Object *obj, Value &ret) {
    Value tag;
    if (!ctx.arguments(0, &tag)) return;
    if (auto target = obj->as<LeastWorkLoadBalancer>()->select(tag)) {
      ret.set(target);
    }
  });

  method("deselect", [](Context &ctx, Object *obj, Value &ret) {
    Str *target = nullptr;
    if (!ctx.arguments(1, &target)) return;
    obj->as<LeastWorkLoadBalancer>()->deselect(target);
  });
}

template<> void ClassDef<Constructor<LeastWorkLoadBalancer>>::init() {
  super<Function>();
  ctor();
}

//
// ResourcePool
//

template<> void ClassDef<ResourcePool>::init() {
  ctor([](Context &ctx) -> Object* {
    Function *allocator;
    if (!ctx.arguments(1, &allocator)) return nullptr;
    return ResourcePool::make(allocator);
  });

  method("allocate", [](Context &ctx, Object *obj, Value &ret) {
    Value pool, tenant;
    if (!ctx.arguments(0, &pool, &tenant)) return;
    obj->as<ResourcePool>()->allocate(ctx, pool, tenant, ret);
  });

  method("free", [](Context &ctx, Object *obj, Value &ret) {
    Value resource;
    if (!ctx.arguments(1, &resource)) return;
    obj->as<ResourcePool>()->free(resource);
  });

  method("freeTenant", [](Context &ctx, Object *obj, Value &ret) {
    Value tenant;
    if (!ctx.arguments(1, &tenant)) return;
    obj->as<ResourcePool>()->free_tenant(tenant);
  });
}

template<> void ClassDef<Constructor<ResourcePool>>::init() {
  super<Function>();
  ctor();
}

//
// Algo
//

template<> void ClassDef<Algo>::init() {
  ctor();
  variable("Cache", class_of<Constructor<Cache>>());
  variable("URLRouter", class_of<Constructor<URLRouter>>());
  variable("HashingLoadBalancer", class_of<Constructor<HashingLoadBalancer>>());
  variable("RoundRobinLoadBalancer", class_of<Constructor<RoundRobinLoadBalancer>>());
  variable("LeastWorkLoadBalancer", class_of<Constructor<LeastWorkLoadBalancer>>());
  variable("ResourcePool", class_of<Constructor<ResourcePool>>());
}

} // namespace pjs