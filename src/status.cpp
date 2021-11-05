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
#include "pjs/pjs.hpp"
#include "api/json.hpp"
#include "utils.hpp"

namespace pipy {

Status Status::local;

void Status::update_modules() {
  modules.clear();

  std::map<std::string, std::set<PipelineDef*>> all_modules;
  PipelineDef::for_each([&](PipelineDef *p) {
    if (auto mod = p->module()) {
      if (mod->worker() == Worker::current()) {
        auto &set = all_modules[mod->path()];
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
}

bool Status::from_json(const Data &data) {
  static pjs::Ref<pjs::Str> key_timestamp(pjs::Str::make("timestamp"));
  static pjs::Ref<pjs::Str> key_uuid(pjs::Str::make("uuid"));
  static pjs::Ref<pjs::Str> key_version(pjs::Str::make("version"));
  static pjs::Ref<pjs::Str> key_modules(pjs::Str::make("modules"));
  static pjs::Ref<pjs::Str> key_filename(pjs::Str::make("filename"));
  static pjs::Ref<pjs::Str> key_graph(pjs::Str::make("graph"));

  pjs::Value json;
  pjs::Value val_timestamp;
  pjs::Value val_uuid;
  pjs::Value val_version;
  pjs::Value val_modules;

  if (!JSON::decode(data, json)) return false;
  if (!json.is_object() || !json.o()) return false;

  auto *root = json.o();
  root->get(key_timestamp, val_timestamp);
  root->get(key_uuid, val_uuid);
  root->get(key_version, val_version);
  root->get(key_modules, val_modules);

  if (!val_timestamp.is_number()) return false;
  if (!val_uuid.is_string()) return false;
  if (!val_version.is_string()) return false;
  if (!val_modules.is_object() || !val_modules.o()) return false;

  timestamp = val_timestamp.n();
  uuid = val_uuid.s()->str();
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

  return true;
}

void Status::to_json(std::ostream &out) const {
  out << "{\"timestamp\":" << uint64_t(timestamp);
  out << ",\"uuid\":\"" << uuid << '"';
  out << ",\"version\":\"" << utils::escape(version) << '"';
  out << ",\"modules\":{";
  bool first = true;
  for (const auto &mod : modules) {
    if (first) first = false; else out << ',';
    out << '"' << utils::escape(mod.filename) << "\":{\"graph\"";
    out << ':' << mod.graph << '}';
  }
  out << "}}";
}

} // namespace pipy