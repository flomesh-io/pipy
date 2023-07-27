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

double Status::LocalInstance::since;
std::string Status::LocalInstance::uuid;
std::string Status::LocalInstance::name;

void Status::update_global() {
  since = Status::LocalInstance::since;
  uuid = Status::LocalInstance::uuid;
  name = Status::LocalInstance::name;

  log_names.clear();
  logging::Logger::get_names(
    [this](const std::string &name) {
      log_names.insert(name);
    }
  );
  timestamp = utils::now();
}

void Status::update_local() {
  modules.clear();
  pools.clear();
  objects.clear();
  chunks.clear();
  pipelines.clear();
  inbounds.clear();
  outbounds.clear();

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
    Graph g;
    Graph::from_pipelines(g, i.second);
    std::string error;
    std::stringstream ss;
    g.to_json(error, ss);
    modules.insert({ i.first, ss.str() });
  }

  for (const auto &p : pjs::Pool::all()) {
    auto *c = p.second;
    if (c->allocated() + c->pooled() > 1) {
      pools.insert({
        c->name(),
        (size_t)c->size(),
        (size_t)c->allocated(),
        (size_t)c->pooled(),
      });
    }
  }

  for (const auto &i : pjs::Class::all()) {
    static const std::string prefix("pjs::Constructor");
    if (utils::starts_with(i.second->name()->str(), prefix)) continue;
    if (auto n = i.second->object_count()) {
      objects.insert({ i.first, (int)n });
    }
  }

  Data::Producer::for_each([&](Data::Producer *producer) {
    chunks.insert({
      producer->name()->str(),
      (size_t)producer->current(),
      (size_t)producer->peak(),
    });
  });

  PipelineLayout::for_each([&](PipelineLayout *p) {
    if (auto mod = dynamic_cast<JSModule*>(p->module())) {
      pipelines.insert({
        mod->filename()->str(),
        p->name_or_label()->str(),
        mod->worker() != Worker::current(),
        (int)p->active(),
        (int)p->allocated(),
      });
    }
  });

  Listener::for_each([&](Listener *listener) {
    auto protocol = Protocol::UNKNOWN;
    switch (listener->protocol()) {
      case Listener::Protocol::TCP: protocol = Status::Protocol::TCP; break;
      case Listener::Protocol::UDP: protocol = Status::Protocol::UDP; break;
      default: break;
    }
    int count = 0;
    int buffered = 0;
    listener->for_each_inbound([&](Inbound *inbound) {
      count++;
      buffered += inbound->get_buffered();
    });
    inbounds.insert({
      protocol,
      listener->ip(),
      (int)listener->port(),
      count,
      buffered,
    });
  });

  std::map<int, OutboundInfo> outbound_tcp, outbound_udp;
  Outbound::for_each([&](Outbound *outbound) {
    std::map<int, OutboundInfo> *m = nullptr;
    auto protocol = Protocol::UNKNOWN;
    auto buffered = 0;
    switch (outbound->protocol()) {
      case Outbound::Protocol::TCP:
        protocol = Protocol::TCP;
        m = &outbound_tcp;
        buffered = static_cast<OutboundTCP*>(outbound)->buffered();
        break;
      case Outbound::Protocol::UDP:
        protocol = Protocol::UDP;
        m = &outbound_udp;
        break;
    }
    if (m) {
      auto port = outbound->port();
      auto &info = (*m)[port];
      info.protocol = protocol;
      info.port = port;
      info.connections++;
      info.buffered += buffered;
    }
  });
  for (auto &p : outbound_tcp) outbounds.insert(p.second);
  for (auto &p : outbound_udp) outbounds.insert(p.second);
}

template<class T>
inline static void merge_sets(std::set<T> &a, const std::set<T> &b) {
  for (const auto &i : b) {
    typename std::set<T>::iterator p = a.find(i);
    if (p == a.end()) {
      a.insert(i);
    } else {
      *p += i;
    }
  }
}

void Status::merge(const Status &other) {
  merge_sets(modules, other.modules);
  merge_sets(pools, other.pools);
  merge_sets(objects, other.objects);
  merge_sets(chunks, other.chunks);
  merge_sets(pipelines, other.pipelines);
  merge_sets(inbounds, other.inbounds);
  merge_sets(outbounds, other.outbounds);
}

bool Status::from_json(const Data &data) {
  thread_local static pjs::Ref<pjs::Str> key_timestamp(pjs::Str::make("timestamp"));
  thread_local static pjs::Ref<pjs::Str> key_uuid(pjs::Str::make("uuid"));
  thread_local static pjs::Ref<pjs::Str> key_name(pjs::Str::make("name"));
  thread_local static pjs::Ref<pjs::Str> key_ip(pjs::Str::make("ip"));
  thread_local static pjs::Ref<pjs::Str> key_version(pjs::Str::make("version"));
  thread_local static pjs::Ref<pjs::Str> key_modules(pjs::Str::make("modules"));
  thread_local static pjs::Ref<pjs::Str> key_filename(pjs::Str::make("filename"));
  thread_local static pjs::Ref<pjs::Str> key_graph(pjs::Str::make("graph"));
  thread_local static pjs::Ref<pjs::Str> key_logs(pjs::Str::make("logs"));

  pjs::Value json;
  pjs::Value val_timestamp;
  pjs::Value val_uuid;
  pjs::Value val_name;
  pjs::Value val_ip;
  pjs::Value val_version;
  pjs::Value val_modules;
  pjs::Value val_logs;

  if (!JSON::decode(data, nullptr, json)) return false;
  if (!json.is_object() || !json.o()) return false;

  auto *root = json.o();
  root->get(key_timestamp, val_timestamp);
  root->get(key_uuid, val_uuid);
  root->get(key_name, val_name);
  root->get(key_ip, val_ip);
  root->get(key_version, val_version);
  root->get(key_modules, val_modules);
  root->get(key_logs, val_logs);

  if (!val_timestamp.is_number()) return false;
  if (!val_uuid.is_string()) return false;
  if (!val_name.is_string()) return false;
  if (!val_ip.is_string()) return false;
  if (!val_version.is_string()) return false;
  if (!val_modules.is_object() || !val_modules.o()) return false;
  if (!val_logs.is_array()) return false;

  timestamp = val_timestamp.n();
  uuid = val_uuid.s()->str();
  name = val_name.s()->str();
  ip = val_ip.s()->str();
  version = val_version.s()->str();

  val_modules.o()->iterate_all(
    [this](pjs::Str *k, pjs::Value &v) {
      if (!v.is_object() || !v.o()) return;
      pjs::Value val_graph;
      v.o()->get(key_graph, val_graph);
      if (!val_graph.is_object() || !val_graph.o()) return;
      modules.insert({
        k->str(),
        JSON::stringify(val_graph, nullptr, 0),
      });
    }
  );

  log_names.clear();
  val_logs.as<pjs::Array>()->iterate_all(
    [this](pjs::Value &v, int) {
      if (v.is_string()) {
        log_names.insert(v.s()->str());
      }
    }
  );

  return true;
}

void Status::to_json(std::ostream &out) const {
  out << "{\"timestamp\":" << uint64_t(timestamp);
  out << ",\"since\":" << uint64_t(since);
  out << ",\"uuid\":\"" << uuid << '"';
  out << ",\"name\":\"" << name << '"';
  out << ",\"ip\":\"" << ip << '"';
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
    out << '"' << utils::escape(name) << '"';
  }
  out << "]}";
}

template<class T>
static void print_table(Data::Builder &db, const T &header, const std::list<T> &rows) {
  static const std::string spacing("  ");

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
  std::list<std::array<std::string, 4>> rows;
  for (const auto &i : pools) {
    rows.push_back({
      i.name,
      std::to_string(i.size * (i.allocated + i.pooled)),
      std::to_string(i.allocated),
      std::to_string(i.pooled),
    });
  }
  print_table(db, { "POOL", "SIZE", "#USED", "#SPARE" }, rows);
}

void Status::dump_objects(Data::Builder &db) {
  std::list<std::array<std::string, 2>> rows;
  for (const auto &i : objects) {
    rows.push_back({ i.name, std::to_string(i.count) });
  }
  print_table(db, { "CLASS", "#INSTANCES" }, rows);
}

void Status::dump_chunks(Data::Builder &db) {
  std::list<std::array<std::string, 3>> rows;
  for (const auto &i : chunks) {
    rows.push_back({
      i.name,
      std::to_string(DATA_CHUNK_SIZE * i.current / 1024),
      std::to_string(DATA_CHUNK_SIZE * i.peak / 1024),
    });
  }
  print_table(db, { "DATA", "CURRENT(KB)", "PEAK(KB)" }, rows);
}

void Status::dump_pipelines(Data::Builder &db) {
  static const std::string s_draining("Draining");
  static const std::string s_running("Running");
  std::list<std::array<std::string, 5>> rows;
  for (const auto &i : pipelines) {
    rows.push_back({
      i.module,
      i.name,
      i.stale ? s_draining : s_running,
      std::to_string(i.allocated),
      std::to_string(i.active),
    });
  }
  print_table(db, { "MODULE", "PIPELINE", "STATE", "#ALLOCATED", "#ACTIVE" }, rows);
}

void Status::dump_inbound(Data::Builder &db) {
  static const std::string s_tcp("TCP");
  static const std::string s_udp("UDP");
  static const std::string s_unknown("?");
  std::list<std::array<std::string, 5>> rows;
  for (const auto &i : inbounds) {
    const std::string *protocol = &s_unknown;
    switch (i.protocol) {
      case Protocol::TCP: protocol = &s_tcp; break;
      case Protocol::UDP: protocol = &s_udp; break;
      default: break;
    }
    rows.push_back({
      *protocol,
      i.ip,
      std::to_string(i.port),
      std::to_string(i.connections),
      std::to_string(i.buffered/1024),
    });
  }
  print_table(db, { "INBOUND", "IP", "PORT", "#CONNECTIONS", "BUFFERED(KB)" }, rows);
}

void Status::dump_outbound(Data::Builder &db) {
  static const std::string s_tcp("TCP");
  static const std::string s_udp("UDP");
  static const std::string s_unknown("?");
  std::list<std::array<std::string, 4>> rows;
  for (const auto &i : outbounds) {
    const std::string *protocol = &s_unknown;
    switch (i.protocol) {
      case Protocol::TCP: protocol = &s_tcp; break;
      case Protocol::UDP: protocol = &s_udp; break;
      default: break;
    }
    rows.push_back({
      *protocol,
      std::to_string(i.port),
      std::to_string(i.connections),
      std::to_string(i.buffered/1024),
    });
  }
  print_table(db, { "OUTBOUND", "PORT", "#CONNECTIONS", "BUFFERED(KB)" }, rows);
}

void Status::dump_json(Data::Builder &db) {
  bool first;
  db.push('{');
  db.push("\"pools\":{");
  first = true;
  for (const auto &i : pools) {
    if (first) first = false; else db.push(',');
    db.push('"');
    db.push(i.name);
    db.push("\":{\"size\":");
    db.push(std::to_string(i.size * (i.allocated + i.pooled)));
    db.push(",\"allocated\":");
    db.push(std::to_string(i.allocated));
    db.push(",\"pooled\":");
    db.push(std::to_string(i.pooled));
    db.push('}');
  }
  db.push("},\"chunks\":{");
  first = true;
  for (const auto &i : chunks) {
    if (first) first = false; else db.push(',');
    db.push('"');
    db.push(i.name);
    db.push("\":{\"current\":");
    db.push(std::to_string(DATA_CHUNK_SIZE * i.current / 1024));
    db.push(",\"peak\":");
    db.push(std::to_string(DATA_CHUNK_SIZE * i.peak / 1024));
    db.push('}');
  }
  db.push("},\"objects\":{");
  first = true;
  for (const auto &i : objects) {
    if (first) first = false; else db.push(',');
    db.push('"');
    db.push(i.name);
    db.push("\":");
    db.push(std::to_string(i.count));
  }
  db.push("},\"pipelines\":[");
  first = true;
  for (const auto &i : pipelines) {
    if (first) first = false; else db.push(',');
    db.push("{\"module\":\"");
    db.push(utils::escape(i.module));
    db.push("\",\"name\":\"");
    db.push(utils::escape(i.name));
    db.push("\",\"allocated\":");
    db.push(std::to_string(i.allocated));
    db.push(",\"active\":");
    db.push(std::to_string(i.active));
    db.push(",\"stale\":");
    db.push(i.stale ? "true" : "false");
    db.push('}');
  }
  db.push("],\"inbound\":[");
  first = true;
  for (const auto &i : inbounds) {
    if (first) first = false; else db.push(',');
    db.push("{\"ip\":\"");
    db.push(i.ip);
    db.push("\",\"port\":");
    db.push(std::to_string(i.port));
    db.push(",\"protocol\":\"");
    switch (i.protocol) {
      case Protocol::TCP: db.push("TCP"); break;
      case Protocol::UDP: db.push("UDP"); break;
      default: break;
    }
    db.push("\",\"connections\":");
    db.push(std::to_string(i.connections));
    db.push(",\"buffered\":");
    db.push(std::to_string(i.buffered/1024));
    db.push('}');
  }
  db.push("],\"outbound\":[");
  first = true;
  for (const auto &i : outbounds) {
    if (first) first = false; else db.push(',');
    db.push("{\"port\":");
    db.push(std::to_string(i.port));
    db.push(",\"protocol\":\"");
    switch (i.protocol) {
      case Protocol::TCP: db.push("TCP"); break;
      case Protocol::UDP: db.push("UDP"); break;
      default: break;
    }
    db.push("\",\"connections\":");
    db.push(std::to_string(i.connections));
    db.push(",\"buffered\":");
    db.push(std::to_string(i.buffered/1024));
    db.push('}');
  }
  db.push(']');
  db.push('}');
}

} // namespace pipy
