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
#include "context.hpp"
#include "utils.hpp"
#include "log.hpp"

namespace pipy {
namespace algo {

//
// Algo
//

auto Algo::hash(const pjs::Value &value) -> size_t {
  std::hash<pjs::Value> hash;
  auto h = hash(value);
  return h;
}

//
// Cache::Options
//

Cache::Options::Options(pjs::Object *options) {
  static pjs::ConstStr str_size("size"), str_ttl("ttl");
  Value(options, str_size)
    .get(size)
    .check_nullable();
  Value(options, str_ttl)
    .get_seconds(ttl)
    .check_nullable();
}

//
// Cache
//

Cache::Cache(const Options &options, pjs::Function *allocate, pjs::Function *free)
  : m_options(options)
  , m_allocate(allocate)
  , m_free(free)
  , m_cache(pjs::OrderedHash<pjs::Value, Entry>::make())
{
  m_options.ttl *= 1000;
}

Cache::~Cache() {
}

bool Cache::get(pjs::Context &ctx, const pjs::Value &key, pjs::Value &value) {
  auto now = (m_options.ttl > 0 ? utils::now() : 0);
  Entry entry;
  bool found = m_cache->use(key, entry);
  if (found) {
    if (m_options.ttl > 0) {
      if (now >= entry.ttl) {
        found = false;
      }
    }
  }
  if (!found) {
    if (!m_allocate) return false;
    pjs::Value arg(key);
    (*m_allocate)(ctx, 1, &arg, value);
    if (!ctx.ok()) return false;
    entry.value = value;
    entry.ttl = now + m_options.ttl;
    m_cache->set(key, entry);
    return true;
  } else {
    value = entry.value;
    return true;
  }
}

void Cache::set(pjs::Context &ctx, const pjs::Value &key, const pjs::Value &value) {
  auto now = (m_options.ttl > 0 ? utils::now() : 0);
  Entry entry;
  entry.value = value;
  entry.ttl = now + m_options.ttl;
  if (m_cache->set(key, entry)) {
    if (m_options.size > 0 && m_cache->size() > m_options.size) {
      int n = m_cache->size() - m_options.size;
      pjs::OrderedHash<pjs::Value, Entry>::Iterator it(m_cache);
      if (m_free) {
        pjs::Value argv[2], ret;
        while (auto *p = it.next()) {
          argv[0] = p->k;
          argv[1] = p->v.value;
          (*m_free)(ctx, 2, argv, ret);
          if (!ctx.ok()) break;
          m_cache->erase(p->k);
          if (!--n) break;
        }
      }
      if (n > 0) {
        while (auto *p = it.next()) {
          m_cache->erase(p->k);
          if (!--n) break;
        }
      }
    }
  }
}

bool Cache::find(const pjs::Value &key, pjs::Value &value) {
  Entry entry;
  bool found = m_cache->use(key, entry);
  if (!found) return false;
  if (m_options.ttl > 0) {
    auto now = utils::now();
    if (now >= entry.ttl) {
      m_cache->erase(key);
      return false;
    }
  }
  value = entry.value;
  return true;
}

bool Cache::remove(pjs::Context &ctx, const pjs::Value &key) {
  if (m_free) {
    Entry entry;
    auto found = m_cache->get(key, entry);
    if (found) {
      if (m_options.ttl > 0) {
        auto now = utils::now();
        if (now >= entry.ttl) found = false;
      }
      pjs::Value argv[2], ret;
      argv[0] = key;
      argv[1] = entry.value;
      (*m_free)(ctx, 2, argv, ret);
      m_cache->erase(key);
    }
    return found;
  } else {
    return m_cache->erase(key);
  }
}

bool Cache::clear(pjs::Context &ctx) {
  if (m_free) {
    pjs::OrderedHash<pjs::Value, Entry>::Iterator it(m_cache);
    while (auto *p = it.next()) {
      pjs::Value argv[2], ret;
      argv[0] = p->k;
      argv[1] = p->v.value;
      (*m_free)(ctx, 2, argv, ret);
      if (!ctx.ok()) return false;
    }
  }
  m_cache->clear();
  return true;
}

//
// Quota
//

Quota::Options::Options(pjs::Object *options) {
  Value(options, "per")
    .get_seconds(per)
    .check_nullable();
}

Quota::Quota(double initial_value, const Options &options)
  : m_options(options)
  , m_initial_value(initial_value)
{
}

void Quota::reset() {
  if (m_current_value >= m_initial_value) {
    m_current_value = m_initial_value;
  } else {
    produce(m_initial_value - m_current_value);
  }
}

void Quota::produce(double value) {
  if (value <= 0) return;
  m_current_value += value;
  while (m_current_value > 0) {
    if (auto *consumer = m_consumers.head()) {
      auto remain = consumer->on_consume(m_current_value);
      if (remain < 0) remain = 0; else
      if (remain > m_current_value) remain = m_current_value;
      m_current_value = remain;
    } else {
      break;
    }
  }
}

auto Quota::consume(double value) -> double {
  if (value <= 0) return 0;
  if (value > m_current_value) value = m_current_value;
  m_current_value -= value;
  schedule_producing();
  return value;
}

void Quota::schedule_producing() {
  if (m_is_producing_scheduled) return;
  if (m_options.per <= 0) return;
  m_timer.schedule(
    m_options.per,
    [this]() {
      m_is_producing_scheduled = false;
      produce(m_initial_value - m_current_value);
    }
  );
  m_is_producing_scheduled = true;
}

//
// Quota::Consumer
//

void Quota::Consumer::set_quota(Quota *quota) {
  if (quota != m_quota) {
    if (m_quota) {
      m_quota->m_consumers.remove(this);
    }
    m_quota = quota;
    m_quota->m_consumers.push(this);
  }
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
// LoadBalancer
//

LoadBalancer::~LoadBalancer() {
  for (const auto &i : m_sessions) {
    close_session(i.second);
  }
  for (const auto &i : m_targets) {
    auto &resources = i.second->resources;
    while (auto r = resources.head()) {
      resources.remove(r);
      r->release();
    }
  }
}

auto LoadBalancer::next(pjs::Object *session_key, const pjs::Value &target_key) -> Resource* {
  if (!session_key) {
    auto id = select(target_key);
    if (!id) return nullptr;
    return Resource::make(id);
  }

  auto &session = m_sessions[session_key];
  if (session) {
    return session->resource();
  } else {
    session = new Session(this, session_key);
  }

  auto id = select(target_key);
  if (!id) return nullptr;

  auto &target = m_targets[id];
  if (!target) {
    target = new Target;
  }

  auto &resources = target->resources;
  auto res = resources.tail();
  if (!res) {
    res = Resource::make(id);
    res->retain();
  } else {
    resources.remove(res);
  }

  session->resource(res);
  return res;
}

void LoadBalancer::close_session(Session *session) {
  if (auto *res = session->resource()) {
    deselect(res->id());
    auto &target = m_targets[res->id()];
    if (!target) target = new Target;
    target->resources.push(res);
  }
  m_sessions.erase(session->key());
  delete session;
}

//
// LoadBalancer::Session
//

LoadBalancer::Session::Session(LoadBalancer *lb, pjs::Object *key)
  : m_lb(lb)
  , m_key(key)
{
  watch(key->weak_ptr());
}

void LoadBalancer::Session::on_weak_ptr_gone() {
  m_lb->close_session(this);
}

//
// HashingLoadBalancer
//

HashingLoadBalancer::HashingLoadBalancer(pjs::Array *targets, Cache *unhealthy)
  : m_targets(targets ? targets->length() : 0)
  , m_unhealthy(unhealthy)
{
  if (targets) {
    targets->iterate_all(
      [this](pjs::Value &v, int i) {
        auto s = v.to_string();
        m_targets[i] = s;
        s->release();
      }
    );
  }
}

HashingLoadBalancer::~HashingLoadBalancer()
{
}

void HashingLoadBalancer::add(pjs::Str *target) {
  m_targets.push_back(target);
}

auto HashingLoadBalancer::select(const pjs::Value &key) -> pjs::Str* {
  if (m_targets.empty()) return nullptr;
  std::hash<pjs::Value> hash;
  auto h = hash(key);
  auto s = m_targets[h % m_targets.size()].get();
  if (s && m_unhealthy) {
    pjs::Value v;
    if (m_unhealthy->find(s, v) && v.to_boolean()) {
      return nullptr;
    }
  }
  return s;
}

//
// RoundRobinLoadBalancer
//

RoundRobinLoadBalancer::RoundRobinLoadBalancer(pjs::Object *targets, Cache *unhealthy)
  : m_unhealthy(unhealthy)
{
  if (targets) {
    if (targets->is_array()) {
      targets->as<pjs::Array>()->iterate_all(
        [this](pjs::Value &v, int) {
          auto *s = v.to_string();
          set(s, 1);
          s->release();
        }
      );
    } else {
      targets->iterate_all(
        [this](pjs::Str *k, pjs::Value &v) {
          set(k, v.to_number());
        }
      );
    }
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

auto RoundRobinLoadBalancer::select(const pjs::Value &key) -> pjs::Str* {
  double min = 0;
  std::map<pjs::Ref<pjs::Str>, Target>::value_type *p = nullptr;
  int total_weight = 0;

  for (auto &i : m_targets) {
    auto &t = i.second;
    if (t.weight <= 0) continue;
    if (m_unhealthy) {
      pjs::Value v;
      if (m_unhealthy->find(i.first.get(), v) && v.to_boolean()) {
        continue;
      }
    }
    total_weight += t.weight;
    if (!p || t.usage < min) {
      min = t.usage;
      p = &i;
    }
  }

  if (!p) return nullptr;

  auto &t = p->second;
  t.hits++;
  t.usage = double(t.hits) / t.weight;

  m_total_hits++;
  if (m_total_hits >= total_weight) {
    for (auto &i : m_targets) {
      auto &t = i.second;
      t.hits = 0;
      t.usage = 0;
    }
    m_total_hits = 0;
  }

  return p->first;
}

//
// LeastWorkLoadBalancer
//

LeastWorkLoadBalancer::LeastWorkLoadBalancer(pjs::Object *targets, Cache *unhealthy)
  : m_unhealthy(unhealthy)
{
  if (targets) {
    if (targets->is_array()) {
      targets->as<pjs::Array>()->iterate_all(
        [this](pjs::Value &v, int) {
          auto *s = v.to_string();
          set(s, 1);
          s->release();
        }
      );
    } else {
      targets->iterate_all(
        [this](pjs::Str *k, pjs::Value &v) {
          set(k, v.to_number());
        }
      );
    }
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

auto LeastWorkLoadBalancer::select(const pjs::Value &key) -> pjs::Str* {
  double min = 0;
  std::map<pjs::Ref<pjs::Str>, Target>::value_type *p = nullptr;
  for (auto &i : m_targets) {
    auto &t = i.second;
    if (t.weight <= 0) continue;
    if (m_unhealthy) {
      pjs::Value v;
      if (m_unhealthy->find(i.first.get(), v) && v.to_boolean()) {
        continue;
      }
    }
    if (!p || t.usage < min) {
      min = t.usage;
      p = &i;
    }
  }

  if (!p) return nullptr;

  auto &t = p->second;
  t.hits++;
  t.usage = double(t.hits) / t.weight;

  return p->first;
}

void LeastWorkLoadBalancer::deselect(pjs::Str *target) {
  if (target) {
    auto i = m_targets.find(target);
    if (i != m_targets.end()) {
      auto &t = i->second;
      if (t.hits > 0) {
        t.hits--;
        t.usage = double(t.hits) / t.weight;
      }
    }
  }
}

//
// ResourcePool
//

void ResourcePool::allocate(pjs::Context &ctx, const pjs::Value &tag, pjs::Value &resource) {
  auto &p = m_pools[tag];
  if (p.empty()) {
    pjs::Value arg(tag);
    (*m_allocator)(ctx, 1, &arg, resource);
    if (!ctx.ok()) return;
  } else {
    resource = p.front();
    p.pop_front();
  }

  m_allocated[resource].tag = tag;
}

void ResourcePool::free(const pjs::Value &resource) {
  auto i = m_allocated.find(resource);
  if (i == m_allocated.end()) return;
  auto &a = i->second;
  m_pools[a.tag].push_back(resource);
  m_allocated.erase(i);
}

//
// Percentile
//

Percentile::Percentile(pjs::Array *buckets)
  : m_counts(buckets->length())
  , m_buckets(buckets->length())
{
  double last = std::numeric_limits<double>::min();
  buckets->iterate_all(
    [&](pjs::Value &v, int i) {
      auto limit = v.to_number();
      if (limit <= last) {
        Log::warn(
          "buckets are not in ascending order: changed from %f to %f at #%d",
          last, limit, i
        );
      }
      m_buckets[i] = limit;
      last = limit;
    }
  );

  reset();
}

void Percentile::reset() {
  for (auto &n : m_counts) n = 0;
  m_sample_count = 0;
}

auto Percentile::get(int bucket) -> size_t {
  if (0 <= bucket && bucket < m_counts.size()) {
    return m_counts[bucket];
  } else {
    return 0;
  }
}

void Percentile::set(int bucket, size_t count) {
  if (0 <= bucket && bucket < m_counts.size()) {
    auto &n = m_counts[bucket];
    if (count > n) m_sample_count += count - n; else
    if (count < n) m_sample_count -= n - count;
    n = count;
  }
}

void Percentile::observe(double sample) {
  for (size_t i = 0, n = m_counts.size(); i < n; i++) {
    if (sample <= m_buckets[i]) {
      m_counts[i]++;
      m_sample_count++;
      break;
    }
  }
}

auto Percentile::calculate(int percentage) -> double {
  if (percentage <= 0) return 0;
  size_t total = m_sample_count * percentage / 100;
  size_t count = 0;
  for (size_t i = 0, n = m_buckets.size(); i < n; i++) {
    count += m_buckets[i];
    if (count >= total) {
      return m_buckets[i];
    }
  }
  return std::numeric_limits<double>::infinity();
}

void Percentile::dump(const std::function<void(double, size_t)> &cb) {
  size_t sum = 0;
  for (size_t i = 0; i < m_buckets.size(); i++) {
    sum += m_counts[i];
    cb(m_buckets[i], sum);
  }
}

} // namespace algo
} // namespace pipy

namespace pjs {

using namespace pipy;
using namespace pipy::algo;

//
// Cache
//

template<> void ClassDef<Cache>::init() {
  ctor([](Context &ctx) -> Object* {
    Function *allocate = nullptr, *free = nullptr;
    Object *options = nullptr;
    if (!ctx.arguments(0, &allocate, &free, &options)) return nullptr;
    return Cache::make(options, allocate, free);
  });

  method("get", [](Context &ctx, Object *obj, Value &ret) {
    Value key;
    if (!ctx.arguments(1, &key)) return;
    obj->as<Cache>()->get(ctx, key, ret);
  });

  method("set", [](Context &ctx, Object *obj, Value &ret) {
    Value key, val;
    if (!ctx.arguments(2, &key, &val)) return;
    obj->as<Cache>()->set(ctx, key, val);
  });

  method("remove", [](Context &ctx, Object *obj, Value &ret) {
    Value key;
    if (!ctx.arguments(1, &key)) return;
    ret.set(obj->as<Cache>()->remove(ctx, key));
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
// Quota
//

template<> void ClassDef<Quota>::init() {
  ctor([](Context &ctx) -> Object* {
    double initial_value = 0;
    pjs::Object *options = nullptr;
    if (!ctx.arguments(0, &initial_value, &options)) return nullptr;
    return Quota::make(initial_value, options);
  });

  accessor("initial", [](pjs::Object *obj, pjs::Value &ret) { ret.set(obj->as<Quota>()->initial()); });
  accessor("current", [](pjs::Object *obj, pjs::Value &ret) { ret.set(obj->as<Quota>()->current()); });

  method("reset", [](Context &ctx, Object *obj, Value &ret) {
    obj->as<Quota>()->reset();
  });

  method("produce", [](Context &ctx, Object *obj, Value &ret) {
    double value;
    if (!ctx.arguments(1, &value)) return;
    obj->as<Quota>()->produce(value);
  });

  method("consume", [](Context &ctx, Object *obj, Value &ret) {
    double value;
    if (!ctx.arguments(1, &value)) return;
    ret.set(obj->as<Quota>()->consume(value));
  });
}

template<> void ClassDef<Constructor<Quota>>::init() {
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
    for (int i = 0; i < ctx.argc(); i++) {
      const auto &seg = ctx.arg(i);
      if (!seg.is_nullish()) {
        auto s = seg.to_string();
        if (url.empty()) {
          url = s->str();
        } else {
          url = pipy::utils::path_join(url, s->str());
        }
        s->release();
      }
    }
    obj->as<URLRouter>()->find(url, ret);
  });
}

template<> void ClassDef<Constructor<URLRouter>>::init() {
  super<Function>();
  ctor();
}

//
// LoadBalancer
//

template<> void ClassDef<LoadBalancer>::init() {
  method("next", [](Context &ctx, Object *obj, Value &ret) {
    pjs::Object *session_key = nullptr;
    pjs::Value target_key;
    if (!ctx.arguments(0, &session_key, &target_key)) return;
    if (!session_key) session_key = static_cast<pipy::Context*>(ctx.root())->inbound();
    ret.set(obj->as<LoadBalancer>()->next(session_key, target_key));
  });

  method("select", [](Context &ctx, Object *obj, Value &ret) {
    pjs::Value key;
    if (!ctx.arguments(0, &key)) return;
    if (auto target = obj->as<LoadBalancer>()->select(key)) {
      ret.set(target);
    }
  });

  method("deselect", [](Context &ctx, Object *obj, Value &ret) {
    Str *target = nullptr;
    if (!ctx.arguments(0, &target)) return;
    obj->as<LoadBalancer>()->deselect(target);
  });
}

//
// LoadBalancer::Resource
//

template<> void ClassDef<LoadBalancer::Resource>::init() {
  accessor("id", [](Object *obj, Value &val) { val.set(obj->as<LoadBalancer::Resource>()->id()); });
}

//
// HashingLoadBalancer
//

template<> void ClassDef<HashingLoadBalancer>::init() {
  super<LoadBalancer>();

  ctor([](Context &ctx) -> Object* {
    Array *targets = nullptr;
    Cache *unhealthy = nullptr;
    if (!ctx.arguments(0, &targets, &unhealthy)) return nullptr;
    return HashingLoadBalancer::make(targets, unhealthy);
  });

  method("add", [](Context &ctx, Object *obj, Value &ret) {
    Str *target;
    if (!ctx.arguments(1, &target)) return;
    obj->as<HashingLoadBalancer>()->add(target);
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
  super<LoadBalancer>();

  ctor([](Context &ctx) -> Object* {
    Object *targets = nullptr;
    Cache *unhealthy = nullptr;
    if (!ctx.arguments(0, &targets, &unhealthy)) return nullptr;
    return RoundRobinLoadBalancer::make(targets, unhealthy);
  });

  method("set", [](Context &ctx, Object *obj, Value &ret) {
    Str *target;
    int weight;
    if (!ctx.arguments(2, &target, &weight)) return;
    obj->as<RoundRobinLoadBalancer>()->set(target, weight);
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
  super<LoadBalancer>();

  ctor([](Context &ctx) -> Object* {
    Object *targets = nullptr;
    Cache *unhealthy = nullptr;
    if (!ctx.arguments(0, &targets, &unhealthy)) return nullptr;
    return LeastWorkLoadBalancer::make(targets, unhealthy);
  });

  method("set", [](Context &ctx, Object *obj, Value &ret) {
    Str *target;
    int weight;
    if (!ctx.arguments(2, &target, &weight)) return;
    obj->as<LeastWorkLoadBalancer>()->set(target, weight);
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
    Value tag;
    if (!ctx.arguments(0, &tag)) return;
    obj->as<ResourcePool>()->allocate(ctx, tag, ret);
  });

  method("free", [](Context &ctx, Object *obj, Value &ret) {
    Value resource;
    if (!ctx.arguments(1, &resource)) return;
    obj->as<ResourcePool>()->free(resource);
  });
}

template<> void ClassDef<Constructor<ResourcePool>>::init() {
  super<Function>();
  ctor();
}

//
// Percentile
//

template<> void ClassDef<Percentile>::init() {
  ctor([](Context &ctx) -> Object* {
    Array *buckets;
    if (!ctx.check(0, buckets)) return nullptr;
    try {
      return Percentile::make(buckets);
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });

  method("reset", [](Context &ctx, Object *obj, Value &ret) {
    obj->as<Percentile>()->reset();
  });

  method("observe", [](Context &ctx, Object *obj, Value &ret) {
    double sample;
    if (!ctx.arguments(1, &sample)) return;
    obj->as<Percentile>()->observe(sample);
  });

  method("calculate", [](Context &ctx, Object *obj, Value &ret) {
    int percentage;
    if (!ctx.arguments(1, &percentage)) return;
    ret.set(obj->as<Percentile>()->calculate(percentage));
  });
}

template<> void ClassDef<Constructor<Percentile>>::init() {
  super<Function>();
  ctor();
}

//
// Algo
//

template<> void ClassDef<Algo>::init() {
  ctor();
  variable("Cache", class_of<Constructor<Cache>>());
  variable("Quota", class_of<Constructor<Quota>>());
  variable("URLRouter", class_of<Constructor<URLRouter>>());
  variable("HashingLoadBalancer", class_of<Constructor<HashingLoadBalancer>>());
  variable("RoundRobinLoadBalancer", class_of<Constructor<RoundRobinLoadBalancer>>());
  variable("LeastWorkLoadBalancer", class_of<Constructor<LeastWorkLoadBalancer>>());
  variable("ResourcePool", class_of<Constructor<ResourcePool>>());
  variable("Percentile", class_of<Constructor<Percentile>>());

  method("hash", [](Context &ctx, Object *obj, Value &ret) {
    Value value;
    if (!ctx.arguments(0, &value)) return;
    auto h = Algo::hash(value);
    ret.set(double(h & ((1ull << 53)-1)));
  });

  method("uuid", [](Context &ctx, Object *obj, Value &ret) {
    std::string str;
    utils::gen_uuid_v4(str);
    ret.set(Str::make(std::move(str)));
  });
}

} // namespace pjs
