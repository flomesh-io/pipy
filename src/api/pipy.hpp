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

#ifndef API_PIPY_H
#define API_PIPY_H

#include "pjs/pjs.hpp"
#include "data.hpp"
#include "pipeline.hpp"
#include "codebase.hpp"

#include <functional>
#include <string>
#include <vector>

namespace pipy {

class File;
class FileStream;
class Worker;

//
// Pipy
//

class Pipy : public pjs::FunctionTemplate<Pipy> {
public:
  struct ExecOptions : public Options {
    bool std_err = false;
    pjs::Ref<Data> std_in;
    pjs::Ref<pjs::Function> on_exit_f;
    ExecOptions() {}
    ExecOptions(pjs::Object *options);
  };

  struct ExecResult {
    pjs::Ref<Data> out;
    pjs::Ref<Data> err;
    int exit_code = 0;
  };

  class FileReader :
    public pjs::RefCount<FileReader>,
    public pjs::Pooled<FileReader>,
    public EventTarget,
    public Pipeline::ResultCallback
  {
  public:
    FileReader(Worker *worker, pjs::Str *pathname, PipelineLayout *pt);

    auto start(const pjs::Value &args) -> pjs::Promise*;

  private:
    pjs::Ref<Worker> m_worker;
    pjs::Ref<pjs::Str> m_pathname;
    pjs::Ref<PipelineLayout> m_pt;
    pjs::Ref<Pipeline> m_pipeline;
    pjs::Ref<File> m_file;
    pjs::Ref<pjs::Promise> m_promise;
    pjs::Ref<pjs::Promise::Settler> m_settler;
    pjs::Value m_start_arg;

    void on_open(FileStream *fs);

    virtual void on_event(Event *evt) override;
    virtual void on_pipeline_result(Pipeline *p, pjs::Value &result) override;
  };

  class FileWatcher :
    public pjs::RefCount<FileWatcher>,
    public pjs::Pooled<FileWatcher>
  {
  public:
    FileWatcher(pjs::Str *pathname);

    auto start() -> pjs::Promise*;

  private:
    Net& m_net;
    pjs::Ref<pjs::Str> m_pathname;
    pjs::Ref<pjs::Promise> m_promise;
    pjs::Ref<pjs::Promise::Settler> m_settler;
    pjs::Ref<Codebase::Watch> m_codebase_watch;

    void on_file_changed(bool changed);
  };

  static auto version() -> pjs::Object*;
  static auto argv() -> pjs::Array*;
  static void argv(const std::vector<std::string> &argv);
  static void on_exit(const std::function<void(int)> &on_exit);
  static auto exec(const std::string &cmd, const ExecOptions &options = ExecOptions()) -> ExecResult;
  static auto exec(pjs::Array *args, const ExecOptions &options = ExecOptions()) -> ExecResult;
  static void listen(pjs::Context &ctx);
  static auto watch(pjs::Str *pathname) -> pjs::Promise*;
  static void exit(int code);
  static void exit(pjs::Function *cb);
  static bool has_exit_callbacks();
  static bool start_exiting(pjs::Context &ctx, const std::function<void()> &on_done);

  void operator()(pjs::Context &ctx, pjs::Object *obj, pjs::Value &ret);

  //
  // Pipy::Inbound
  //

  class Inbound : public pjs::ObjectTemplate<Inbound> {};

  //
  // Pipy::Outbound
  //

  class Outbound : public pjs::ObjectTemplate<Outbound> {};
};

} // namespace pipy

#endif // API_PIPY_H
