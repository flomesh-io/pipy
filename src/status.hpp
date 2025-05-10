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

#ifndef STATUS_HPP
#define STATUS_HPP

#include "data.hpp"

#include <ostream>
#include <set>
#include <string>

namespace pipy {

class Data;

class Status {
public:
  enum class Protocol {
    UNKNOWN,
    TCP,
    UDP,
    NETLINK,
  };

  struct LocalInstance {
    static double since;
    static std::string source;
    static std::string uuid;
    static std::string name;
  };

  struct ModuleInfo {
    std::string filename;
    std::string graph;

    bool operator<(const ModuleInfo &r) const {
      return filename < r.filename;
    }

    auto operator+=(const ModuleInfo &r) const -> const ModuleInfo& {
      return *this;
    }
  };

  struct PoolInfo {
    std::string name;
    size_t size;
    mutable size_t allocated;
    mutable size_t pooled;

    bool operator<(const PoolInfo &r) const {
      return name < r.name;
    }

    auto operator+=(const PoolInfo &r) const -> const PoolInfo& {
      allocated += r.allocated;
      pooled += r.pooled;
      return *this;
    }
  };

  struct ObjectInfo {
    std::string name;
    mutable int count;

    bool operator<(const ObjectInfo &r) const {
      return name < r.name;
    }

    auto operator+=(const ObjectInfo &r) const -> const ObjectInfo& {
      count += r.count;
      return *this;
    }
  };

  struct ChunkInfo {
    std::string name;
    mutable size_t count;

    bool operator<(const ChunkInfo &r) const {
      return name < r.name;
    }

    auto operator+=(const ChunkInfo &r) const -> const ChunkInfo& {
      count += r.count;
      return *this;
    }
  };

  struct PipelineInfo {
    std::string module;
    std::string name;
    bool stale;
    mutable int active;
    mutable int allocated;

    bool operator<(const PipelineInfo &r) const {
      if (stale < r.stale) return true;
      if (stale > r.stale) return false;
      if (module < r.module) return true;
      if (module > r.module) return false;
      return name < r.name;
    }

    auto operator+=(const PipelineInfo &r) const -> const PipelineInfo& {
      active += r.active;
      allocated += r.allocated;
      return *this;
    }
  };

  struct BufferInfo {
    std::string name;
    mutable size_t size;

    bool operator<(const BufferInfo &r) const {
      return name < r.name;
    }

    auto operator+=(const BufferInfo &r) const -> const BufferInfo& {
      size += r.size;
      return *this;
    }
  };

  struct InboundInfo {
    Protocol protocol;
    std::string ip;
    int port;
    mutable int connections;
    mutable int buffered;

    bool operator<(const InboundInfo &r) const {
      if (protocol < r.protocol) return true;
      if (protocol > r.protocol) return false;
      if (ip < r.ip) return true;
      if (ip > r.ip) return false;
      return port < r.port;
    }

    auto operator+=(const InboundInfo &r) const -> const InboundInfo& {
      connections += r.connections;
      buffered += r.buffered;
      return *this;
    }
  };

  struct OutboundInfo {
    Protocol protocol = Protocol::UNKNOWN;
    int port = 0;
    mutable int connections = 0;
    mutable int buffered = 0;

    bool operator<(const OutboundInfo &r) const {
      if (protocol < r.protocol) return true;
      if (protocol > r.protocol) return false;
      return port < r.port;
    }

    auto operator+=(const OutboundInfo &r) const -> const OutboundInfo& {
      connections += r.connections;
      buffered += r.buffered;
      return *this;
    }
  };

  double since = 0;
  double timestamp = 0;
  std::string uuid;
  std::string name;
  std::string ip;
  std::string version;
  std::set<PoolInfo> pools;
  std::set<ObjectInfo> objects;
  std::set<ChunkInfo> chunks;
  std::set<BufferInfo> buffers;
  std::set<InboundInfo> inbounds;
  std::set<OutboundInfo> outbounds;
  std::set<std::string> log_names;

  void update_global();
  void update_local();
  void merge(const Status &other);
  bool from_json(const Data &data, Data *metrics = nullptr);
  void to_json(Data::Builder &db, Data *metrics = nullptr) const;
  void dump_pools(Data::Builder &db);
  void dump_objects(Data::Builder &db);
  void dump_chunks(Data::Builder &db);
  void dump_buffers(Data::Builder &db);
  void dump_inbound(Data::Builder &db);
  void dump_outbound(Data::Builder &db);
  void dump_json(Data::Builder &db);
};

} // namespace pipy

#endif // STATUS_HPP
