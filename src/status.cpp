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

#include "status.hpp"
#include "worker.hpp"
#include "module.hpp"
#include "pipeline.hpp"
#include "graph.hpp"
#include "listener.hpp"
#include "outbound.hpp"
#include "pjs/pjs.hpp"
#include "api/json.hpp"
#include "api/logging.hpp"
#include "filters/http2.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

namespace pipy {

thread_local Status Status::local;

void Status::update() {
  modules.clear();

  std::map<std::string, std::set<PipelineLayout*>> all_modules;
  PipelineLayout::for_each([&](PipelineLayout *p) {
    if (auto mod = dynamic_cast<JSModule*>(p->module())) {
      if (mod->worker() == Worker::current()) {
        auto &set = all_modules[mod->filename()->str()];
        set.insert(p);
      }
    }
  });

  for (const auto &i : all_modules) {
    modules.emplace_back();
    auto &mod = modules.back();
    Graph g;
    Graph::from_pipelines(g, i.second);
    std::string error;
    std::stringstream ss;
    g.to_json(error, ss);
    mod.filename = i.first;
    mod.graph = ss.str();
  }

  log_names.clear();
  logging::Logger::for_each(
    [&](logging::Logger *logger) {
      log_names.push_back(logger->name());
    }
  );
}

bool Status::from_json(const Data &data) {
  static pjs::Ref<pjs::Str> key_timestamp(pjs::Str::make("timestamp"));
  static pjs::Ref<pjs::Str> key_uuid(pjs::Str::make("uuid"));
  static pjs::Ref<pjs::Str> key_name(pjs::Str::make("name"));
  static pjs::Ref<pjs::Str> key_version(pjs::Str::make("version"));
  static pjs::Ref<pjs::Str> key_modules(pjs::Str::make("modules"));
  static pjs::Ref<pjs::Str> key_filename(pjs::Str::make("filename"));
  static pjs::Ref<pjs::Str> key_graph(pjs::Str::make("graph"));
  static pjs::Ref<pjs::Str> key_logs(pjs::Str::make("logs"));

  pjs::Value json;
  pjs::Value val_timestamp;
  pjs::Value val_uuid;
  pjs::Value val_name;
  pjs::Value val_version;
  pjs::Value val_modules;
  pjs::Value val_logs;

  if (!JSON::decode(data, json)) return false;
  if (!json.is_object() || !json.o()) return false;

  auto *root = json.o();
  root->get(key_timestamp, val_timestamp);
  root->get(key_uuid, val_uuid);
  root->get(key_name, val_name);
  root->get(key_version, val_version);
  root->get(key_modules, val_modules);
  root->get(key_logs, val_logs);

  if (!val_timestamp.is_number()) return false;
  if (!val_uuid.is_string()) return false;
  if (!val_name.is_string()) return false;
  if (!val_version.is_string()) return false;
  if (!val_modules.is_object() || !val_modules.o()) return false;
  if (!val_logs.is_array()) return false;

  timestamp = val_timestamp.n();
  uuid = val_uuid.s()->str();
  name = val_name.s()->str();
  version = val_version.s()->str();

  val_modules.o()->iterate_all(
    [this](pjs::Str *k, pjs::Value &v) {
      if (!v.is_object() || !v.o()) return;
      pjs::Value val_graph;
      v.o()->get(key_graph, val_graph);
      if (!val_graph.is_object() || !val_graph.o()) return;
      modules.emplace_back();
      auto &mod = modules.back();
      mod.filename = k->str();
      mod.graph = JSON::stringify(val_graph, nullptr, 0);
    }
  );

  log_names.clear();
  val_logs.as<pjs::Array>()->iterate_all(
    [this](pjs::Value &v, int) {
      if (v.is_string()) {
        log_names.push_back(v.s());
      }
    }
  );

  return true;
}

void Status::to_json(std::ostream &out) const {
  out << "{\"timestamp\":" << uint64_t(timestamp);
  out << ",\"uuid\":\"" << uuid << '"';
  out << ",\"name\":\"" << name << '"';
  out << ",\"version\":\"" << utils::escape(version) << '"';
  out << ",\"modules\":{";
  bool first = true;
  for (const auto &mod : modules) {
    if (first) first = false; else out << ',';
    out << '"' << utils::escape(mod.filename) << "\":{\"graph\"";
    out << ':' << mod.graph << '}';
  }
  out << "},\"logs\":[";
  first = true;
  for (const auto &name : log_names) {
    if (first) first = false; else out << ',';
    out << '"' << utils::escape(name->str()) << '"';
  }
  out << "]}";
}

template<class T>
static void print_table(Data::Builder &db, const T &header, const std::list<T> &rows) {
  static std::string spacing("  ");

  int n = header.size();
  int max_width[header.size()];

  for (int i = 0; i < n; i++) {
    max_width[i] = header[i].length();
  }

  for (const auto &row : rows) {
    for (int i = 0; i < n; i++) {
      if (row[i].length() > max_width[i]) {
        max_width[i] = row[i].length();
      }
    }
  }

  int total_width = 0;
  for (int i = 0; i < n; i++) {
    total_width += max_width[i] + 2;
  }

  db.push(std::string(total_width, '-'));
  db.push('\n');

  for (int i = 0; i < n; i++) {
    std::string padding(max_width[i] - header[i].length(), ' ');
    db.push(header[i]);
    db.push(padding);
    db.push(spacing);
  }

  db.push('\n');

  for (const auto &row : rows) {
    for (int i = 0; i < n; i++) {
      std::string padding(max_width[i] - row[i].length(), ' ');
      db.push(row[i]);
      db.push(padding);
      db.push(spacing);
    }
    db.push('\n');
  }
}

void Status::dump_pools(Data::Builder &db) {
  std::list<std::array<std::string, 4>> pools;

  for (const auto &p : pjs::Pool::all()) {
    auto *c = p.second;
    if (c->allocated() + c->pooled() > 1) {
      pools.push_back({
        c->name(),
        std::to_string(c->size() * (c->allocated() + c->pooled())),
        std::to_string(c->allocated()),
        std::to_string(c->pooled()),
      });
    }
  }

  print_table(db, { "POOL", "SIZE", "#USED", "#SPARE" }, pools);
}

void Status::dump_objects(Data::Builder &db) {
  std::list<std::array<std::string, 2>> objects;
  int total_instances = 0;

  for (const auto &i : pjs::Class::all()) {
    static std::string prefix("pjs::Constructor");
    if (utils::starts_with(i.second->name()->str(), prefix)) continue;
    if (auto n = i.second->object_count()) {
      objects.push_back({ i.first, std::to_string(n) });
      total_instances += n;
    }
  }

  objects.push_back({ "TOTAL", std::to_string(total_instances) });
  print_table(db, { "CLASS", "#INSTANCES" }, objects );
}

void Status::dump_chunks(Data::Builder &db) {
  std::list<std::array<std::string, 3>> chunks;
  int total_chunks = 0;

  Data::Producer::for_each([&](Data::Producer *producer) {
    chunks.push_back({
      producer->name()->str(),
      std::to_string(producer->current() * DATA_CHUNK_SIZE / 1024),
      std::to_string(producer->peak() * DATA_CHUNK_SIZE / 1024),
    });
    total_chunks += producer->current();
  });

  chunks.push_back({ "TOTAL", std::to_string(total_chunks * DATA_CHUNK_SIZE / 1024), "n/a" });
  print_table(db, { "DATA", "CURRENT(KB)", "PEAK(KB)" }, chunks );
}

void Status::dump_pipelines(Data::Builder &db) {
  std::list<std::array<std::string, 3>> pipelines;
  std::multimap<std::string, PipelineLayout*> stale_pipelines;
  std::multimap<std::string, PipelineLayout*> current_pipelines;
  int total_allocated = 0;
  int total_active = 0;

  auto current_worker = Worker::current();

  PipelineLayout::for_each([&](PipelineLayout *p) {
    if (auto mod = dynamic_cast<JSModule*>(p->module())) {
      std::string name(mod->filename()->str());
      name += " [";
      name += p->name() == pjs::Str::empty ? p->label()->str() : p->name()->str();
      name += ']';
      if (mod->worker() == current_worker) {
        current_pipelines.insert({ name, p });
      } else if (p->active() > 0) {
        name = std::string("[STALE] ") + name;
        stale_pipelines.insert({ name, p });
      }
    }
  });

  for (const auto &i : current_pipelines) {
    auto p = i.second;
    pipelines.push_back({
      i.first,
      std::to_string(p->allocated()),
      std::to_string(p->active()),
    });
    total_allocated += p->allocated();
    total_active += p->active();
  }

  for (const auto &i : stale_pipelines) {
    auto p = i.second;
    pipelines.push_back({
      i.first,
      std::to_string(p->allocated()),
      std::to_string(p->active()),
    });
    total_allocated += p->allocated();
    total_active += p->active();
  }

  pipelines.push_back({
    "TOTAL",
    std::to_string(total_allocated),
    std::to_string(total_active),
  });

  print_table(db, { "PIPELINE", "#ALLOCATED", "#ACTIVE" }, pipelines);
}

void Status::dump_inbound(Data::Builder &db) {
  std::list<std::array<std::string, 3>> inbounds;
  int total_inbound_connections = 0;
  int total_inbound_buffered = 0;

  Listener::for_each([&](Listener *listener) {
    int count = 0;
    int buffered = 0;
    listener->for_each_inbound([&](Inbound *inbound) {
      count++;
      buffered += inbound->size_in_buffer();
    });
    char count_peak[100];
    sprintf(count_peak, "%d/%d", count, listener->peak_connections());
    inbounds.push_back({
      std::to_string(listener->port()),
      std::string(count_peak),
      std::to_string(buffered/1024),
    });
    total_inbound_connections += count;
    total_inbound_buffered += buffered;
  });

  inbounds.push_back({
    "TOTAL",
    std::to_string(total_inbound_connections),
    std::to_string(total_inbound_buffered),
  });

  print_table(db, { "INBOUND", "#CONNECTIONS", "BUFFERED(KB)" }, inbounds);
}

void Status::dump_outbound(Data::Builder &db) {
  std::list<std::array<std::string, 6>> outbounds;

  struct OutboundSum {
    int connections = 0;
    double max_connection_time = 0;
    double avg_connection_time = 0;
  };

  OutboundSum outbound_total;
  std::unordered_map<std::string, OutboundSum> outbound_sums;
  Outbound::for_each([&](Outbound *outbound) {
    char key[1000];
    std::sprintf(key, "%s [%s]:%d",
      outbound->protocol_name()->c_str(),
      outbound->host().c_str(),
      outbound->port()
    );
    auto conn_time = outbound->connection_time() / (outbound->retries() + 1);
    auto &sum = outbound_sums[key];
    sum.connections++;
    sum.max_connection_time = std::max(sum.max_connection_time, conn_time);
    sum.avg_connection_time += conn_time;
    outbound_total.connections++;
    outbound_total.max_connection_time = std::max(outbound_total.max_connection_time, conn_time);
    outbound_total.avg_connection_time += conn_time;
  });

  int i = 0;
  std::vector<const std::pair<const std::string, OutboundSum> *> ranks(outbound_sums.size());
  for (const auto &p : outbound_sums) ranks[i++] = &p;

  std::sort(
    ranks.begin(), ranks.end(),
    [](
      const std::pair<const std::string, OutboundSum> *a,
      const std::pair<const std::string, OutboundSum> *b)
    {
      return a->second.connections > b->second.connections;
    }
  );

  const int max_outbounds = 10;

  for (int i = 0; i < max_outbounds && i < ranks.size(); i++) {
    const auto name = ranks[i]->first;
    const auto &sum = ranks[i]->second;
    outbounds.push_back({
      name,
      std::to_string(sum.connections),
      std::to_string(int(sum.max_connection_time)),
      std::to_string(int(sum.avg_connection_time / sum.connections)),
    });
  }

  if (ranks.size() > max_outbounds) {
    char str[100];
    std::sprintf(str, "  (%d more...)", int(ranks.size() - max_outbounds));
    outbounds.push_back({ str, "", "", "", "", "" });
  }

  outbounds.push_back({
    "TOTAL",
    std::to_string(outbound_total.connections),
    std::to_string(int(outbound_total.max_connection_time)),
    std::to_string(int(outbound_total.connections ? outbound_total.avg_connection_time / outbound_total.connections : 0)),
  });

  print_table(db, { "OUTBOUND", "#CONNECTIONS", "MAX_CONN_TIME", "AVG_CONN_TIME" }, outbounds);
}

} // namespace pipy
