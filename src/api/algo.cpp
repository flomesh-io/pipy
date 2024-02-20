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
  thread_local static pjs::ConstStr str_size("size"), str_ttl("ttl");
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

Cache::~Cache()
{
}

bool Cache::get(pjs::Context &ctx, const pjs::Value &key, pjs::Value &value) {
  return get(
    key, value,
    [&](pjs::Value &value) {
      if (!m_allocate) return false;
      pjs::Value arg(key);
      (*m_allocate)(ctx, 1, &arg, value);
      return ctx.ok();
    }
  );
}

void Cache::set(pjs::Context &ctx, const pjs::Value &key, const pjs::Value &value) {
  set(
    key, value,
    [&](const pjs::Value &key, const pjs::Value &value) {
      if (!m_free) return true;
      pjs::Value argv[2], ret;
      argv[0] = key;
      argv[1] = value;
      (*m_free)(ctx, 2, argv, ret);
      return ctx.ok();
    }
  );
}

bool Cache::get(const pjs::Value &key, pjs::Value &value) {
  return get(key, value, nullptr);
}

void Cache::set(const pjs::Value &key, const pjs::Value &value) {
  set(key, value, nullptr);
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

bool Cache::remove(const pjs::Value &key) {
  return m_cache->erase(key);
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

bool Cache::get(
  const pjs::Value &key, pjs::Value &value,
  const std::function<bool(pjs::Value &)> &allocate
) {
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
    if (!allocate) return false;
    if (!allocate(value)) return false;
    entry.value = value;
    entry.ttl = now + m_options.ttl;
    m_cache->set(key, entry);
    return true;
  } else {
    value = entry.value;
    return true;
  }
}

void Cache::set(
  const pjs::Value &key, const pjs::Value &value,
  const std::function<bool(const pjs::Value &, const pjs::Value &)> &free
) {
  auto now = (m_options.ttl > 0 ? utils::now() : 0);
  Entry entry;
  entry.value = value;
  entry.ttl = now + m_options.ttl;
  if (m_cache->set(key, entry)) {
    if (m_options.size > 0 && m_cache->size() > m_options.size) {
      int n = m_cache->size() - m_options.size;
      pjs::OrderedHash<pjs::Value, Entry>::Iterator it(m_cache);
      if (free) {
        pjs::Value argv[2], ret;
        while (auto *p = it.next()) {
          if (!free(p->k, p->v.value)) break;
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

//
// Quota
//

Quota::Options::Options(pjs::Object *options) {
  Value(options, "per")
    .get_seconds(per)
    .check_nullable();
  Value(options, "produce")
    .get(produce)
    .check_nullable();
}

Quota::Quota(double initial_value, const Options &options)
  : m_options(options)
  , m_initial_value(initial_value)
  , m_current_value(initial_value)
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
  retain();
  while (auto c = m_consumers.head()) {
    m_consumers.remove(c);
    c->m_quota = nullptr;
    if (!c->on_consume(this)) {
      c->m_quota = this;
      m_consumers.unshift(c);
      break;
    }
    if (m_current_value <= 0) break;
  }
  release();
}

void Quota::produce_async(double value) {
  retain();
  Net::current().post(
    [=]() {
      InputContext ic;
      produce(value);
      release();
    }
  );
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
      double value = m_initial_value - m_current_value;
      if (m_options.produce > 0 && m_options.produce < value) {
        schedule_producing();
        value = m_options.produce;
      }
      produce(value);
    }
  );
  m_is_producing_scheduled = true;
}

void Quota::enqueue(Consumer *consumer) {
  if (!consumer->m_quota) {
    consumer->m_quota = this;
    m_consumers.push(consumer);
  }
}

void Quota::dequeue(Consumer *consumer) {
  if (consumer->m_quota == this) {
    m_consumers.remove(consumer);
    consumer->m_quota = nullptr;
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
  static const std::string s_slash("/");
  static const std::string s_asterisk("*");

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

LoadBalancer::Options::Options(pjs::Object *options) {
  Value(options, "algorithm")
    .get_enum<LoadBalancer::Algorithm>(algorithm)
    .check_nullable();
  Value(options, "key")
    .get(key_f)
    .check_nullable();
  Value(options, "weight")
    .get(weight_f)
    .check_nullable();
  Value(options, "capacity")
    .get(capacity)
    .get(capacity_f)
    .check_nullable();
}

void LoadBalancer::provision(pjs::Context &ctx, pjs::Array *targets) {
  if (targets) {
    std::map<pjs::Value, Pool*> new_targets;
    std::vector<pjs::Ref<Pool>> new_pools(targets->length());
    List<Pool> new_queue;

    targets->iterate_while(
      [&](pjs::Value &target, int i) {
        pjs::Value key;
        if (m_options.key_f) {
          (*m_options.key_f)(ctx, 1, &target, key);
          if (!ctx.ok()) return false;
        } else key = target;

        Pool* p = nullptr;
        auto it = m_targets.find(key);
        if (it != m_targets.end()) {
          p = it->second;
        } else {
          p = new Pool(key, target);
        }

        new_targets[key] = p;
        new_pools[i] = p;
        new_queue.unshift(p);
        sort_forward(new_queue, p);

        pjs::Value weight, capacity(m_options.capacity);
        if (m_options.weight_f) {
          (*m_options.weight_f)(ctx, 1, &target, weight);
          if (!ctx.ok()) return false;
        }
        if (m_options.capacity_f) {
          (*m_options.capacity_f)(ctx, 1, &target, capacity);
          if (!ctx.ok()) return false;
        }

        p->weight = weight.is_undefined() ? 1 : weight.to_number();
        p->capacity = capacity.is_undefined() ? 0 : capacity.to_int32();

        return true;
      }
    );

    if (!ctx.ok()) return;

    m_targets = std::move(new_targets);
    m_pools = std::move(new_pools);
    m_queue = std::move(new_queue);

  } else {
    m_targets.clear();
    m_pools.clear();
    m_queue.clear();
  }

  double weight_total = 0;
  for (const auto &p : m_pools) weight_total += p->weight;
  for (const auto &p : m_pools) p->step = weight_total / p->weight;

  if (m_options.algorithm == ROUND_ROBIN) {
    for (const auto &p : m_pools) p->load = 0;
    for (const auto &p : m_pools) {
      p->load = p->step;
      sort_forward(m_queue, p);
    }
  }
}

auto LoadBalancer::schedule(int size, Cache *exclusive) -> pjs::Array* {
  if (size < 0) return nullptr;
  auto a = pjs::Array::make(size);
  for (int i = 0; i < size; i++) {
    if (auto p = next(exclusive)) {
      a->set(i, p->target);
    }
  }
  return a;
}

auto LoadBalancer::allocate(pjs::Context &ctx, const pjs::Value &tag, Cache *exclusive) -> Resource* {
  auto p = next(exclusive);
  if (!p) return nullptr;
  return p->allocate();
}

auto LoadBalancer::next(Cache *exclusive) -> Pool* {
  pjs::Value val;
  for (auto p = m_queue.head(); p; p = p->next()) {
    if (!exclusive || !exclusive->find(p->key, val)) {
      increase_load(p);
      return p;
    }
  }
  return nullptr;
}

void LoadBalancer::increase_load(Pool *pool) {
  pool->load += pool->step;
  sort_forward(m_queue, pool);
}

void LoadBalancer::decrease_load(Pool *pool) {
  pool->load -= pool->step;
  sort_backward(m_queue, pool);
}

void LoadBalancer::sort_forward(List<Pool> &queue, Pool *pool) {
  if (auto p = pool->next()) {
    while (p && p->load <= pool->load) {
      p = p->next();
    }
    if (p != pool->next()) {
      queue.remove(pool);
      if (p) {
        queue.insert(pool, p);
      } else {
        queue.push(pool);
      }
    }
  }
}

void LoadBalancer::sort_backward(List<Pool> &queue, Pool *pool) {
  if (auto p = pool->back()) {
    while (p && p->load > pool->load) {
      p = p->back();
    }
    if (p != pool->back()) {
      queue.remove(pool);
      if (p) {
        queue.insert(pool, p->next());
      } else {
        queue.unshift(pool);
      }
    }
  }
}

auto LoadBalancer::Pool::allocate() -> Resource* {
  auto r = m_resources.head();
  if (!r || (r->m_load > 0 && (capacity <= 0 || capacity > m_resources.size()))) {
    r = Resource::make(this, target);
    m_resources.unshift(r);
  }
  r->increase_load();
  return r;
}

LoadBalancer::Resource::~Resource() {
  m_pool->m_resources.remove(this);
}

void LoadBalancer::Resource::free() {
  if (m_load > 0) {
    m_load--;
    if (auto r = back()) {
      while (r && r->m_load > m_load) {
        r = r->back();
      }
      if (r != back()) {
        auto &list = m_pool->m_resources;
        list.remove(this);
        if (r) {
          list.insert(this, r->next());
        } else {
          list.unshift(this);
        }
      }
    }
  }
}

void LoadBalancer::Resource::increase_load() {
  m_load++;
  if (auto r = next()) {
    while (r && r->m_load <= m_load) {
      r = r->next();
    }
    if (r != next()) {
      auto &list = m_pool->m_resources;
      list.remove(this);
      if (r) {
        list.insert(this, r);
      } else {
        list.push(this);
      }
    }
  }
}

//
// LoadBalancerBase
//

LoadBalancerBase::~LoadBalancerBase() {
  for (const auto &i : m_sessions) {
    delete i.second;
  }
  for (const auto &i : m_targets) {
    auto &resources = i.second->resources;
    while (auto r = resources.head()) {
      resources.remove(r);
      r->release();
    }
  }
}

auto LoadBalancerBase::borrow(pjs::Object *borrower, const pjs::Value &target_key, Cache *unhealthy) -> Resource* {
  if (!borrower) {
    auto id = select(target_key, unhealthy);
    if (!id) return nullptr;
    return Resource::make(id);
  }

  auto &s = m_sessions[borrower];
  if (!s) s = new Session(this, borrower);

  auto *session = s;
  auto *res = session->resource();
  if (res && !is_healthy(res->id(), unhealthy)) {
    close_session(session);
    session = new Session(this, borrower);
    m_sessions[borrower] = session;
    res = nullptr;
  }

  if (res) return res;

  auto id = select(target_key, unhealthy);
  if (!id) return nullptr;

  auto &target = m_targets[id];
  if (!target) {
    target = new Target;
  }

  auto &resources = target->resources;
  res = resources.tail();
  if (!res) {
    res = Resource::make(id);
    res->retain();
  } else {
    resources.remove(res);
  }

  session->resource(res);
  return res;
}

bool LoadBalancerBase::is_healthy(pjs::Str *target, Cache *unhealthy) {
  pjs::Value v;
  if (!unhealthy && !m_unhealthy) return true;
  if (m_unhealthy && m_unhealthy->find(target, v) && v.to_boolean()) return false;
  if (unhealthy && unhealthy->find(target, v) && v.to_boolean()) return false;
  return true;
}

void LoadBalancerBase::close_session(Session *session) {
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
// LoadBalancerBase::Session
//

LoadBalancerBase::Session::Session(LoadBalancerBase *lb, pjs::Object *key)
  : m_lb(lb)
  , m_key(key)
{
  watch(key->weak_ptr());
}

void LoadBalancerBase::Session::on_weak_ptr_gone() {
  m_lb->close_session(this);
}

//
// HashingLoadBalancer
//

HashingLoadBalancer::HashingLoadBalancer(pjs::Object *targets, Cache *unhealthy)
  : pjs::ObjectTemplate<HashingLoadBalancer, LoadBalancerBase>(unhealthy)
{
  set(targets);
}

HashingLoadBalancer::~HashingLoadBalancer()
{
}

void HashingLoadBalancer::set(pjs::Object *targets) {
  if (targets) {
    m_targets.clear();
    if (targets->is_array()) {
      targets->as<pjs::Array>()->iterate_all(
        [this](pjs::Value &v, int i) {
          auto s = v.to_string();
          m_targets.push_back(s);
          s->release();
        }
      );
    } else {
      targets->iterate_all(
        [this](pjs::Str *k, pjs::Value &) {
          m_targets.push_back(k);
        }
      );
    }
  }
}

void HashingLoadBalancer::add(pjs::Str *target) {
  m_targets.push_back(target);
}

auto HashingLoadBalancer::select(const pjs::Value &key, Cache *unhealthy) -> pjs::Str* {
  if (m_targets.empty()) return nullptr;
  std::hash<pjs::Value> hash;
  auto h = hash(key);
  auto s = m_targets[h % m_targets.size()].get();
  if (s && !is_healthy(s, unhealthy)) return nullptr;
  return s;
}

//
// RoundRobinLoadBalancer
//

RoundRobinLoadBalancer::RoundRobinLoadBalancer(pjs::Object *targets, Cache *unhealthy)
  : pjs::ObjectTemplate<RoundRobinLoadBalancer, LoadBalancerBase>(unhealthy)
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
          set(k, v.to_int32());
        }
      );
    }
  }
}

RoundRobinLoadBalancer::~RoundRobinLoadBalancer()
{
}

void RoundRobinLoadBalancer::set(pjs::Object *targets) {
  if (!targets) return;
  for (auto &t : m_targets) t.removed = true;

  auto update = [this](pjs::Str *id, int weight) {
    auto i = m_target_map.find(id);
    if (i != m_target_map.end()) {
      auto *t = i->second;
      if (t->weight == weight) {
        t->removed = false;
        return;
      }
    }
    set(id, weight);
  };

  if (targets->is_array()) {
    targets->as<pjs::Array>()->iterate_all(
      [&](pjs::Value &v, int) {
        auto s = v.to_string();
        update(s, 1);
        s->release();
      }
    );
  } else {
    targets->iterate_all(
      [&](pjs::Str *k, pjs::Value &v) {
        update(k, v.to_int32());
      }
    );
  }

  for (auto i = m_targets.begin(); i != m_targets.end(); ) {
    auto p = i++;
    if (p->removed) {
      m_target_map.erase(p->id);
      m_targets.erase(p);
    }
  }
}

void RoundRobinLoadBalancer::set(pjs::Str *target, int weight) {
  if (weight < 0) weight = 0;

  double min_usage = -1;
  for (const auto &t : m_targets) {
    if (min_usage < 0 || t.usage < min_usage) {
      min_usage = t.usage;
    }
  }

  if (min_usage < 0) min_usage = 0;

  auto i = m_target_map.find(target);
  if (i == m_target_map.end()) {
    m_targets.emplace_back();
    auto &t = m_targets.back();
    t.id = target;
    t.weight = weight;
    t.hits = weight * min_usage;
    t.usage = min_usage;
    t.healthy = weight > 0;
    t.removed = false;
    m_target_map[target] = &t;
  } else {
    auto *t = i->second;
    t->weight = weight;
    t->hits = weight * min_usage;
    t->usage = min_usage;
    t->healthy = weight > 0;
    t->removed = false;
  }
}

auto RoundRobinLoadBalancer::select(const pjs::Value &key, Cache *unhealthy) -> pjs::Str* {
  if (!key.is_undefined()) {
    if (!m_target_cache) {
      Cache::Options options;
      m_target_cache = Cache::make(options);
    }
    pjs::Value target;
    if (m_target_cache->get(key, target)) {
      if (m_target_map.count(target.s())) {
        return target.s();
      }
      m_target_cache->remove(key);
    }
  }

  double min = 0;
  Target *p = nullptr;
  int total_weight = 0;
  int total_hits = 0;

  for (auto &t : m_targets) {
    if (!is_healthy(t.id.get(), unhealthy)) t.healthy = false;
    if (!t.healthy || !t.weight) continue;
    total_weight += t.weight;
    total_hits += t.hits;
    if (!p || t.usage < min) {
      min = t.usage;
      p = &t;
    }
  }

  for (auto &t : m_targets) {
    if (!t.healthy && is_healthy(t.id.get(), unhealthy)) {
      t.healthy = true;
      t.usage = min;
      t.hits = t.weight * t.usage;
      if (t.weight) {
        total_weight += t.weight;
        total_hits += t.hits;
        if (!p) p = &t;
      }
    }
  }

  if (!p) return nullptr;

  auto &t = *p;
  t.hits++;
  t.usage = double(t.hits) / t.weight;
  total_hits++;

  if (total_hits >= total_weight) {
    for (auto &t : m_targets) {
      t.hits = 0;
      t.usage = 0;
    }
  }

  if (!key.is_undefined()) {
    m_target_cache->set(key, p->id.get());
  }

  return p->id;
}

//
// LeastWorkLoadBalancer
//

LeastWorkLoadBalancer::LeastWorkLoadBalancer(pjs::Object *targets, Cache *unhealthy)
  : pjs::ObjectTemplate<LeastWorkLoadBalancer, LoadBalancerBase>(unhealthy)
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
          set(k, v.to_int32());
        }
      );
    }
  }
}

LeastWorkLoadBalancer::~LeastWorkLoadBalancer()
{
}

void LeastWorkLoadBalancer::set(pjs::Object *targets) {
  if (!targets) return;
  for (auto &t : m_targets) t.second.removed = true;

  auto update = [this](pjs::Str *id, int weight) {
    auto i = m_targets.find(id);
    if (i != m_targets.end()) {
      auto &t = i->second;
      if (t.weight == weight) {
        t.removed = false;
        return;
      }
    }
    set(id, weight);
  };

  if (targets->is_array()) {
    targets->as<pjs::Array>()->iterate_all(
      [&](pjs::Value &v, int) {
        auto s = v.to_string();
        update(s, 1);
        s->release();
      }
    );
  } else {
    targets->iterate_all(
      [&](pjs::Str *k, pjs::Value &v) {
        update(k, v.to_int32());
      }
    );
  }

  for (auto i = m_targets.begin(); i != m_targets.end(); ) {
    auto p = i++;
    if (p->second.removed) {
      m_targets.erase(p);
    }
  }
}

void LeastWorkLoadBalancer::set(pjs::Str *target, double weight) {
  if (weight < 0) weight = 0;

  auto i = m_targets.find(target);
  if (i == m_targets.end()) {
    auto &t = m_targets[target];
    t.weight = weight;
    t.hits = 0;
    t.usage = 0;
    t.removed = false;
  } else {
    auto &t = i->second;
    t.weight = weight;
    t.usage = double(t.hits) / weight;
    t.removed = false;
  }
}

auto LeastWorkLoadBalancer::select(const pjs::Value &key, Cache *unhealthy) -> pjs::Str* {
  if (!key.is_undefined()) {
    if (!m_target_cache) {
      Cache::Options options;
      m_target_cache = Cache::make(options);
    }
    pjs::Value target;
    if (m_target_cache->get(key, target)) {
      return target.s();
    }
  }

  double min = 0;
  std::map<pjs::Ref<pjs::Str>, Target>::value_type *p = nullptr;
  for (auto &i : m_targets) {
    auto &t = i.second;
    if (t.weight <= 0) continue;
    if (!is_healthy(i.first.get(), unhealthy)) continue;
    if (!p || t.usage < min) {
      min = t.usage;
      p = &i;
    }
  }

  if (!p) return nullptr;

  auto &t = p->second;
  t.hits++;
  t.usage = double(t.hits) / t.weight;

  if (!key.is_undefined()) {
    m_target_cache->set(key, p->first.get());
  }

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

  method("find", [](Context &ctx, Object *obj, Value &ret) {
    Value key;
    if (!ctx.arguments(1, &key)) return;
    if (!obj->as<Cache>()->find(key, ret)) ret = Value::undefined;
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
    try {
      return Quota::make(initial_value, options);
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
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

template<> void EnumDef<LoadBalancer::Algorithm>::init() {
  define(LoadBalancer::ROUND_ROBIN, "round-robin");
  define(LoadBalancer::LEAST_LOAD, "least-load");
}

template<> void ClassDef<LoadBalancer::Resource>::init() {
  accessor("target", [](Object *obj, Value &ret) {
    ret = obj->as<LoadBalancer::Resource>()->target();
  });

  method("free", [](Context &ctx, Object *obj, Value &) {
    obj->as<LoadBalancer::Resource>()->free();
  });
}

template<> void ClassDef<LoadBalancer>::init() {
  ctor([](Context &ctx) -> Object* {
    Array *targets = nullptr;
    Object *options = nullptr;
    if (!ctx.arguments(0, &targets, &options)) return nullptr;
    auto lb = LoadBalancer::make(options);
    lb->provision(ctx, targets);
    return lb;
  });

  method("provision", [](Context &ctx, Object *obj, Value &ret) {
    Array *targets = nullptr;
    if (!ctx.arguments(1, &targets)) return;
    obj->as<LoadBalancer>()->provision(ctx, targets);
  });

  method("schedule", [](Context &ctx, Object *obj, Value &ret) {
    int size;
    Cache *exclusive = nullptr;
    if (!ctx.arguments(1, &size, &exclusive)) return;
    ret.set(obj->as<LoadBalancer>()->schedule(size, exclusive));
  });

  method("allocate", [](Context &ctx, Object *obj, Value &ret) {
    Value tag;
    Cache *exclusive = nullptr;
    if (!ctx.arguments(0, &tag, &exclusive)) return;
    ret.set(obj->as<LoadBalancer>()->allocate(ctx, tag, exclusive));
  });
}

template<> void ClassDef<Constructor<LoadBalancer>>::init() {
  super<Function>();
  ctor();
}

//
// LoadBalancerBase
//

template<> void ClassDef<LoadBalancerBase>::init() {
  method("borrow", [](Context &ctx, Object *obj, Value &ret) {
    pjs::Object *borrower = nullptr;
    pjs::Value target_key;
    Cache *unhealthy = nullptr;
    if (!ctx.arguments(0, &borrower, &target_key, &unhealthy)) return;
    if (!borrower) borrower = static_cast<pipy::Context*>(ctx.root())->inbound();
    ret.set(obj->as<LoadBalancerBase>()->borrow(borrower, target_key, unhealthy));
  });

  method("next", [](Context &ctx, Object *obj, Value &ret) {
    pjs::Object *borrower = nullptr;
    pjs::Value target_key;
    Cache *unhealthy = nullptr;
    if (!ctx.arguments(0, &borrower, &target_key, &unhealthy)) return;
    if (!borrower) borrower = static_cast<pipy::Context*>(ctx.root())->inbound();
    ret.set(obj->as<LoadBalancerBase>()->borrow(borrower, target_key, unhealthy));
  });

  method("select", [](Context &ctx, Object *obj, Value &ret) {
    pjs::Value key;
    Cache *unhealthy = nullptr;
    if (!ctx.arguments(0, &key, &unhealthy)) return;
    if (auto target = obj->as<LoadBalancerBase>()->select(key, unhealthy)) {
      ret.set(target);
    }
  });

  method("deselect", [](Context &ctx, Object *obj, Value &ret) {
    Str *target = nullptr;
    if (!ctx.arguments(0, &target)) return;
    obj->as<LoadBalancerBase>()->deselect(target);
  });
}

//
// LoadBalancerBase::Resource
//

template<> void ClassDef<LoadBalancerBase::Resource>::init() {
  accessor("id", [](Object *obj, Value &val) { val.set(obj->as<LoadBalancerBase::Resource>()->id()); });
}

//
// HashingLoadBalancer
//

template<> void ClassDef<HashingLoadBalancer>::init() {
  super<LoadBalancerBase>();

  ctor([](Context &ctx) -> Object* {
    Object *targets = nullptr;
    Cache *unhealthy = nullptr;
    if (!ctx.arguments(0, &targets, &unhealthy)) return nullptr;
    return HashingLoadBalancer::make(targets, unhealthy);
  });

  method("set", [](Context &ctx, Object *obj, Value &ret) {
    Object *targets;
    if (!ctx.arguments(1, &targets)) return;
    obj->as<HashingLoadBalancer>()->set(targets);
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
  super<LoadBalancerBase>();

  ctor([](Context &ctx) -> Object* {
    Object *targets = nullptr;
    Cache *unhealthy = nullptr;
    if (!ctx.arguments(0, &targets, &unhealthy)) return nullptr;
    return RoundRobinLoadBalancer::make(targets, unhealthy);
  });

  method("set", [](Context &ctx, Object *obj, Value &ret) {
    Object *targets;
    Str *target;
    int weight;
    if (ctx.get(0, targets)) {
      obj->as<RoundRobinLoadBalancer>()->set(targets);
    } else if (ctx.get(0, target)) {
      if (!ctx.check(1, weight)) return;
      obj->as<RoundRobinLoadBalancer>()->set(target, weight);
    } else {
      ctx.error_argument_type(0, "a string or an object");
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
  super<LoadBalancerBase>();

  ctor([](Context &ctx) -> Object* {
    Object *targets = nullptr;
    Cache *unhealthy = nullptr;
    if (!ctx.arguments(0, &targets, &unhealthy)) return nullptr;
    return LeastWorkLoadBalancer::make(targets, unhealthy);
  });

  method("set", [](Context &ctx, Object *obj, Value &ret) {
    Object *targets;
    Str *target;
    int weight;
    if (ctx.get(0, targets)) {
      obj->as<LeastWorkLoadBalancer>()->set(targets);
    } else if (ctx.get(0, target)) {
      if (!ctx.check(1, weight)) return;
      obj->as<LeastWorkLoadBalancer>()->set(target, weight);
    } else {
      ctx.error_argument_type(0, "a string or an object");
    }
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
  variable("LoadBalancer", class_of<Constructor<LoadBalancer>>());
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
    auto str = utils::make_uuid_v4();
    ret.set(Str::make(std::move(str)));
  });
}

} // namespace pjs
