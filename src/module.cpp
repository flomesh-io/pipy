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

#include "module.hpp"
#include "codebase.hpp"
#include "worker.hpp"
#include "pipeline.hpp"
#include "api/configuration.hpp"
#include "api/console.hpp"
#include "api/json.hpp"
#include "graph.hpp"
#include "utils.hpp"
#include "log.hpp"

#include <fstream>
#include <sstream>
#include <iostream>

namespace pipy {

//
// ModuleBase
//

void ModuleBase::for_each_pipeline(const std::function<void(PipelineLayout*)> &cb) {
  for (const auto &p : m_pipelines) {
    cb(p);
  }
}

void ModuleBase::shutdown() {
  retain();
  {
    InputContext ic;
    for (const auto &p : m_pipelines) {
      p->shutdown();
    }
    m_pipelines.clear();
  }
  release();
}

//
// JSModule
//

JSModule::JSModule(Worker *worker, int index)
  : Module(index)
  , m_worker(worker)
  , m_imports(new pjs::Expr::Imports)
{
  Log::debug("[module   %p] ++ index = %d", this, index);
}

JSModule::~JSModule() {
  Log::debug("[module   %p] -- index = %d", this, index());
  m_worker->remove_module(index());
}

auto JSModule::find_named_pipeline(pjs::Str *name) -> PipelineLayout* {
  auto i = m_named_pipelines.find(name);
  if (i == m_named_pipelines.end()) return nullptr;
  return i->second;
}

auto JSModule::find_indexed_pipeline(int index) -> PipelineLayout* {
  auto i = m_indexed_pipelines.find(index);
  if (i == m_indexed_pipelines.end()) return nullptr;
  return i->second;
}

auto JSModule::new_context(Context *base) -> Context* {
  return m_worker->new_runtime_context(base);
}

bool JSModule::load(const std::string &path) {
  m_filename = pjs::Str::make(path);

  auto sd = Codebase::current()->get(path);
  if (!sd) {
    Log::error("[pjs] Cannot open script at %s", path.c_str());
    return false;
  }

  Data data(*sd);
  sd->release();
  m_source.filename = path;
  m_source.content = data.to_string();

  std::string error;
  int error_line, error_column;
  auto expr = pjs::Parser::parse(&m_source, error, error_line, error_column);
  m_script = std::unique_ptr<pjs::Expr>(expr);

  if (!expr) {
    Log::pjs_location(m_source.content, path, error_line, error_column);
    Log::error(
      "[pjs] Syntax error: %s at line %d column %d in %s",
      error.c_str(), error_line, error_column, path.c_str()
    );
    return false;
  }

  pjs::Ref<Context> ctx = m_worker->new_loading_context();
  expr->resolve(*ctx, index(), m_imports.get());

  pjs::Value result;
  if (!expr->eval(*ctx, result)) {
    ctx->backtrace("(root)");
    Log::pjs_error(ctx->error());
    return false;
  }

  if (!result.is_class(pjs::class_of<Configuration>())) {
    Data out;
    Console::dump(result, out);
    out.to_chunks(
      [](const uint8_t *ptr, int len) {
        std::cout.write((const char *)ptr, len);
      }
    );
    std::cout << std::endl;
    Log::error("[pjs] Script did not result in a Configuration");
    return false;
  }

  auto config = result.as<Configuration>();
  try {
    config->check_integrity();
  } catch (std::runtime_error &err) {
    Log::error("[config] %s", err.what());
    return false;
  }

  Graph g;
  config->draw(g);

  error.clear();
  auto lines = g.to_text(error);

  if (m_worker->m_graph_enabled || !error.empty()) {
    std::string title("Module ");
    title += path;

    Log::info("[config]");
    Log::info("[config] %s", title.c_str());
    Log::info("[config] %s", std::string(title.length(), '=').c_str());
    Log::info("[config]");

    for (const auto &l : lines) {
      Log::info("[config]  %s", l.c_str());
    }

  } else {
    Log::info("[config] Module loaded: %s", path.c_str());
  }

  if (!error.empty()) {
    Log::error("[config] %s", error.c_str());
    return false;
  }

  m_configuration = config;

  return true;
}

void JSModule::unload() {
  retain();
  ModuleBase::shutdown();
  m_entrance_pipeline = nullptr;
  m_named_pipelines.clear();
  m_indexed_pipelines.clear();
  release();
}

void JSModule::bind_exports(Worker *worker) {
  m_configuration->bind_exports(worker, this);
}

void JSModule::bind_imports(Worker *worker) {
  m_configuration->bind_imports(worker, this, m_imports.get());
}

void JSModule::make_pipelines() {
  m_configuration->apply(this);
}

void JSModule::bind_pipelines() {
  ModuleBase::for_each_pipeline(
    [](PipelineLayout *p) {
      p->bind();
    }
  );
}

} // namespace pipy
