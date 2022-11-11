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

#include "api/stats.hpp"
#include "data.hpp"

#include <list>
#include <ostream>
#include <string>

namespace pipy {

class Data;

class Status {
public:
  struct ModuleInfo {
    std::string filename;
    std::string graph;
  };

  double timestamp = 0;
  std::string uuid;
  std::string name;
  std::string version;
  std::list<ModuleInfo> modules;
  std::list<pjs::Ref<pjs::Str>> log_names;

  static Status local;
  static pjs::Ref<stats::Counter> metric_inbound_in;
  static pjs::Ref<stats::Counter> metric_inbound_out;
  static pjs::Ref<stats::Counter> metric_outbound_in;
  static pjs::Ref<stats::Counter> metric_outbound_out;
  static pjs::Ref<stats::Histogram> metric_outbound_conn_time;

  void update();
  bool from_json(const Data &data);
  void to_json(std::ostream &out) const;

  static void register_metrics();
  static void dump_memory();
  static void dump_pools(Data::Builder &db);
  static void dump_objects(Data::Builder &db);
  static void dump_chunks(Data::Builder &db);
  static void dump_pipelines(Data::Builder &db);
  static void dump_inbound(Data::Builder &db);
  static void dump_outbound(Data::Builder &db);
  static void dump_http2(Data::Builder &db);
};

} // namespace pipy

#endif // STATUS_HPP
