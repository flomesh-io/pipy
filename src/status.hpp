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

#include <list>
#include <ostream>
#include <string>

namespace pipy {

class Data;

class Status {
public:
  thread_local static Status local;

  enum class Protocol {
    UNKNOWN,
    TCP,
    UDP,
  };

  struct ModuleInfo {
    std::string filename;
    std::string graph;
  };

  struct PoolInfo {
    std::string name;
    int size;
    int allocated;
    int pooled;
  };

  struct ObjectInfo {
    std::string name;
    int count;
  };

  struct ChunkInfo {
    std::string name;
    int current;
    int peak;
  };

  struct PipelineInfo {
    std::string module;
    std::string name;
    bool stale;
    int active;
    int allocated;
  };

  struct InboundInfo {
    Protocol protocol;
    std::string ip;
    int port;
    int connections;
    int buffered;
  };

  struct OutboundInfo {
    Protocol protocol = Protocol::UNKNOWN;
    int port = 0;
    int connections = 0;
    int buffered = 0;
  };

  double since = 0;
  double timestamp = 0;
  std::string uuid;
  std::string name;
  std::string version;
  std::list<ModuleInfo> modules;
  std::list<PoolInfo> pools;
  std::list<ObjectInfo> objects;
  std::list<ChunkInfo> chunks;
  std::list<PipelineInfo> pipelines;
  std::list<InboundInfo> inbounds;
  std::list<OutboundInfo> outbounds;
  std::list<pjs::Ref<pjs::Str>> log_names;

  void update();
  bool from_json(const Data &data);
  void to_json(std::ostream &out) const;
  void dump_pools(Data::Builder &db);
  void dump_objects(Data::Builder &db);
  void dump_chunks(Data::Builder &db);
  void dump_pipelines(Data::Builder &db);
  void dump_inbound(Data::Builder &db);
  void dump_outbound(Data::Builder &db);
};

} // namespace pipy

#endif // STATUS_HPP
