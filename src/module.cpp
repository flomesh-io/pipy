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
#include "graph.hpp"
#include "utils.hpp"
#include "logging.hpp"

#include <fstream>
#include <sstream>

namespace pipy {

Module::Module(Worker *worker, int index)
  : m_index(index)
  , m_worker(worker)
  , m_imports(new pjs::Expr::Imports)
{
  Log::debug("[module   %p] ++ index = %d", this, index);
}

Module::~Module() {
  Log::debug("[module   %p] -- index = %d", this, m_index);
  m_worker->remove_module(m_index);
}

bool Module::load(const std::string &path) {
  auto data = Codebase::current()->get(path);
  if (!data) {
    Log::error("[pjs] Cannot open script at %s", path.c_str());
    return false;
  }
  m_source = data->to_string();

  std::string error;
  int error_line, error_column;
  auto expr = pjs::Parser::parse(m_source, error, error_line, error_column);
  m_script = std::unique_ptr<pjs::Expr>(expr);

  if (!expr) {
    Log::pjs_location(m_source, error_line, error_column);
    Log::error(
      "[pjs] Syntax error: %s at line %d column %d in %s",
      error.c_str(), error_line, error_column, path.c_str()
    );
    return false;
  }

  pjs::Ref<Context> ctx = m_worker->new_loading_context();
  expr->resolve(*ctx, m_index, m_imports.get());

  pjs::Value result;
  if (!expr->eval(*ctx, result)) {
    ctx->backtrace("(root)");
    Log::pjs_error(ctx->error(), m_source);
    return false;
  }

  if (!result.is_class(pjs::class_of<Configuration>())) {
    Console::log(&result, 1);
    Log::error("[pjs] Script was not evaluated to a Configuration");
    return false;
  }

  std::string title("Module ");
  title += path;

  Log::info("[config]");
  Log::info("[config] %s", title.c_str());
  Log::info("[config] %s", std::string(title.length(), '=').c_str());
  Log::info("[config]");

  Graph g;
  auto config = result.as<Configuration>();
  config->draw(g);

  error.clear();
  auto lines = g.to_text(error);
  for (const auto &l : lines) {
    Log::info("[config]  %s", l.c_str());
  }

  if (!error.empty()) {
    Log::error("[config] %s", error.c_str());
    return false;
  }

  m_path = path;
  m_name = pjs::Str::make(path);
  m_filename = pjs::Str::make(path);
  m_configuration = config;

  return true;
}

void Module::unload() {
  retain();
  for (const auto &p : m_pipelines) {
    p->shutdown();
  }
  m_named_pipelines.clear();
  m_indexed_pipelines.clear();
  m_pipelines.clear();
  release();
}

void Module::bind_exports() {
  m_configuration->bind_exports(m_worker, this);
}

void Module::bind_imports() {
  m_configuration->bind_imports(m_worker, this, m_imports.get());
}

void Module::make_pipelines() {
  m_configuration->apply(this);
}

void Module::bind_pipelines() {
  for (const auto &p : m_pipelines) {
    p->bind();
  }
}

auto Module::find_named_pipeline(pjs::Str *name) -> PipelineLayout* {
  auto i = m_named_pipelines.find(name);
  if (i == m_named_pipelines.end()) return nullptr;
  return i->second;
}

auto Module::find_indexed_pipeline(int index) -> PipelineLayout* {
  auto i = m_indexed_pipelines.find(index);
  if (i == m_indexed_pipelines.end()) return nullptr;
  return i->second;
}

} // namespace pipy
