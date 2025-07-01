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
#include "buffer.hpp"
#include "worker.hpp"
#include "worker-thread.hpp"
#include "pipeline.hpp"
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

//
// StatusDeserializer
//

class StatusDeserializer : public JSON::Visitor {
public:
  StatusDeserializer(Status &status)
    : m_status(status)
    , m_capture(m_capture_buffer) {}

  enum class Key {
    unknown,
    timestamp,
    since,
    uuid,
    name,
    ip,
    version,
    modules,
    graph,
    metrics,
    logs,
  };

  struct KeyName {
    Key key;
    const char *name;
  };

  pjs::Ref<Data> metrics;

private:
  enum { MAX_DEPTH = 4 };

  struct Level {
    Key key = Key::unknown;
  };

  Status& m_status;
  Level m_stack[MAX_DEPTH];
  int m_depth = 0;
  bool m_capturing = false;
  bool m_capturing_list_start = false;
  Data::Builder m_capture;
  Data m_capture_buffer;
  std::string m_current_module;

  virtual void null() override {
    if (m_capturing) {
      capture_comma();
      m_capture.push("null");
    }
  }

  virtual void boolean(bool b) override {
    if (m_capturing) {
      capture_comma();
      m_capture.push(b ? "true" : "false");
    }
  }

  virtual void integer(int64_t i) override {
    if (m_capturing) {
      capture_comma();
      char str[100];
      auto len = std::snprintf(str, sizeof(str), "%lld", (long long)i);
      m_capture.push(str, len);
    } else if (m_depth == 1) {
      switch (m_stack[1].key) {
        case Key::timestamp: m_status.timestamp = i; break;
        case Key::since: m_status.since = i; break;
        case Key::version: m_status.version = std::to_string(i); break;
        default: break;
      }
    }
  }

  virtual void number(double n) override {
    if (m_capturing) {
      capture_comma();
      char str[100];
      auto len = pjs::Number::to_string(str, sizeof(str), n);
      m_capture.push(str, len);
    } else if (m_depth == 1) {
      switch (m_stack[1].key) {
        case Key::timestamp: m_status.timestamp = n; break;
        case Key::since: m_status.since = n; break;
        case Key::version: m_status.version = std::to_string(n); break;
        default: break;
      }
    }
  }

  virtual void string(const char *s, size_t len) override {
    if (m_capturing) {
      capture_comma();
      m_capture.push('"');
      utils::escape(s, len, [this](char c) { m_capture.push(c); });
      m_capture.push('"');
    } else if (m_depth == 1) {
      switch (m_stack[1].key) {
        case Key::uuid: m_status.uuid = std::string(s, len); break;
        case Key::name: m_status.name = std::string(s, len); break;
        case Key::ip: m_status.ip = std::string(s, len); break;
        case Key::version: m_status.version = std::string(s, len); break;
        default: break;
      }
    } else if (is_at(Key::logs, Key::unknown)) {
      m_status.log_names.insert(std::string(s, len));
    }
  }

  virtual void map_start() override {
    if (m_capturing) {
      capture_comma();
      m_capture.push('{');
      m_capturing_list_start = true;
    } else if (is_at(Key::modules, Key::unknown, Key::graph) || is_at(Key::metrics)) {
      m_capture_buffer.clear();
      m_capture.reset();
      m_capture.push('{');
      m_capturing = true;
      m_capturing_list_start = true;
    }
    m_depth++;
    if (m_depth < MAX_DEPTH) m_stack[m_depth].key = Key::unknown;
  }

  virtual void map_key(const char *s, size_t len) override {
    if (m_capturing) {
      capture_comma();
      m_capture.push('"');
      utils::escape(s, len, [this](char c) { m_capture.push(c); });
      m_capture.push('"');
      m_capture.push(':');
      m_capturing_list_start = true;
    } else if (is_at(Key::modules, Key::unknown)) {
      m_current_module = std::string(s, len);
    } else if (m_depth < MAX_DEPTH) {
      Key k = Key::unknown;
      for (size_t i = 0; s_key_names[i].name; i++) {
        const auto *name = s_key_names[i].name;
        if (!std::strncmp(name, s, len) && !name[len]) {
          k = s_key_names[i].key;
          break;
        }
      }
      m_stack[m_depth].key = k;
    }
  }

  virtual void map_end() override {
    m_depth--;
    if (m_capturing) {
      m_capture.push('}');
      m_capturing_list_start = false;
      if (is_at(Key::metrics)) {
        m_capture.flush();
        m_capturing = false;
        metrics = Data::make(std::move(m_capture_buffer));
      }
    }
  }

  virtual void array_start() override {
    if (m_capturing) {
      capture_comma();
      m_capture.push('[');
      m_capturing_list_start = true;
    } else if (is_at(Key::logs)) {
      m_status.log_names.clear();
    }
    m_depth++;
    if (m_depth < MAX_DEPTH) m_stack[m_depth].key = Key::unknown;
  }

  virtual void array_end() override {
    m_depth--;
    if (m_capturing) {
      m_capture.push(']');
      m_capturing_list_start = false;
    }
  }

  bool is_at(Key k1) {
    return m_depth == 1 && m_stack[1].key == k1;
  }

  bool is_at(Key k1, Key k2) {
    return m_depth == 2 && m_stack[1].key == k1 && m_stack[2].key == k2;
  }

  bool is_at(Key k1, Key k2, Key k3) {
    return m_depth == 3 && m_stack[1].key == k1 && m_stack[2].key == k2 && m_stack[3].key == k3;
  }

  void capture_comma() {
    if (m_capturing_list_start) {
      m_capturing_list_start = false;
    } else {
      m_capture.push(',');
    }
  }

  static KeyName s_key_names[];
};

StatusDeserializer::KeyName StatusDeserializer::s_key_names[] = {
  { Key::timestamp, "timestamp" },
  { Key::since, "since" },
  { Key::uuid, "uuid" },
  { Key::name, "name" },
  { Key::ip, "ip" },
  { Key::version, "version" },
  { Key::modules, "modules" },
  { Key::graph, "graph" },
  { Key::metrics, "metrics" },
  { Key::logs, "logs" },
  { Key::unknown, nullptr },
};

//
// Status
//

double Status::LocalInstance::since;
std::string Status::LocalInstance::source;
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
  pools.clear();
  objects.clear();
  chunks.clear();
  buffers.clear();
  inbounds.clear();
  outbounds.clear();

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

  if (WorkerThread::current()->index() == 0) {
    Data::Producer::for_each([&](Data::Producer *producer) {
      chunks.insert({
        producer->name(),
        producer->count(),
      });
    });
  }

  BufferStats::for_each(
    [&](BufferStats *bs) {
      if (!bs->name.empty() && bs->size > 0) {
        BufferInfo bi{ bs->name, bs->size };
        auto i = buffers.find(bi);
        if (i != buffers.end()) {
          i->size += bi.size;
        } else {
          buffers.insert(bi);
        }
      }
    }
  );

  Listener::for_each([&](Listener *listener) {
    auto protocol = Protocol::UNKNOWN;
    switch (listener->protocol()) {
      case Port::Protocol::TCP: protocol = Status::Protocol::TCP; break;
      case Port::Protocol::UDP: protocol = Status::Protocol::UDP; break;
      default: break;
    }
    int count = 0;
    int buffered = 0;
    listener->for_each_inbound([&](Inbound *inbound) {
      count++;
      buffered += inbound->get_buffered();
      return true;
    });
    inbounds.insert({
      protocol,
      listener->ip(),
      (int)listener->port(),
      count,
      buffered,
    });
    return true;
  });

  std::map<int, OutboundInfo> outbound_tcp, outbound_udp, outbound_netlink;
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
    return true;
  });
  for (auto &p : outbound_tcp) outbounds.insert(p.second);
  for (auto &p : outbound_udp) outbounds.insert(p.second);
  for (auto &p : outbound_netlink) outbounds.insert(p.second);
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
  merge_sets(pools, other.pools);
  merge_sets(objects, other.objects);
  merge_sets(chunks, other.chunks);
  merge_sets(buffers, other.buffers);
  merge_sets(inbounds, other.inbounds);
  merge_sets(outbounds, other.outbounds);
}

bool Status::from_json(const Data &data, Data *metrics) {
  StatusDeserializer sd(*this);
  if (!JSON::visit(data, &sd)) return false;
  if (metrics && sd.metrics) *metrics = std::move(*sd.metrics);
  return true;
}

void Status::to_json(Data::Builder &db, Data *metrics) const {
  bool first;

  auto push_uint = [&](uint64_t i) {
    char str[100];
    auto len = std::snprintf(str, sizeof(str), "%llu", (unsigned long long)i);
    db.push(str, len);
  };

  auto push_str = [&](const std::string &s) {
    db.push('"');
    utils::escape(s, [&](char c) {
      db.push(c);
    });
    db.push('"');
  };

  db.push("{\"timestamp\":"); push_uint(timestamp);
  db.push(",\"since\":"); push_uint(since);
  db.push(",\"uuid\":"); push_str(uuid);
  db.push(",\"name\":"); push_str(name);
  db.push(",\"ip\":"); push_str(ip);
  db.push(",\"version\":"); push_str(version);

  if (metrics) {
    db.push(",\"metrics\":");
    db.push(std::move(*metrics));
  }

  db.push(",\"logs\":["); first = true;
  for (const auto &name : log_names) {
    if (first) first = false; else db.push(',');
    push_str(name);
  }
  db.push("]}");
}

template<class T>
static void print_table(Data::Builder &db, const T &header, const std::list<T> &rows) {
  static const std::string spacing("  ");

  int n = header.size();
  int max_width[100]; // no more than 100 columns

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
  std::list<std::array<std::string, 2>> rows;
  for (const auto &i : chunks) {
    rows.push_back({
      i.name,
      std::to_string(DATA_CHUNK_SIZE * i.count / 1024),
    });
  }
  print_table(db, { "DATA", "SIZE(KB)" }, rows);
}

void Status::dump_buffers(Data::Builder &db) {
  std::list<std::array<std::string, 2>> rows;
  for (const auto &i : buffers) {
    rows.push_back({
      i.name,
      std::to_string(i.size / 1024),
    });
  }
  print_table(db, { "BUFFER", "SIZE(KB)" }, rows);
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
    db.push("\":");
    db.push(std::to_string(DATA_CHUNK_SIZE * i.count / 1024));
  }
  db.push("},\"buffers\":{");
  first = true;
  for (const auto &i : buffers) {
    if (first) first = false; else db.push(',');
    db.push('"');
    db.push(i.name);
    db.push("\":");
    db.push(std::to_string(i.size / 1024));
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
