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

#ifndef LOGGING_HPP
#define LOGGING_HPP

#include "pjs/pjs.hpp"
#include "options.hpp"
#include "module.hpp"
#include "fstream.hpp"
#include "filters/pack.hpp"
#include "filters/tls.hpp"

#include <atomic>
#include <list>
#include <memory>
#include <set>
#include <functional>

namespace pipy {

class AdminService;
class AdminLink;
class Data;
class Pipeline;
class PipelineLayout;
class MessageStart;
class FileStream;

namespace logging {

//
// Logger
//

class Logger : public pjs::ObjectTemplate<Logger> {
public:
  static void set_admin_service(AdminService *admin_service);
  static void set_admin_link(AdminLink *admin_link);
  static void set_history_limit(size_t size) { s_history_limit = size; }
  static void get_names(const std::function<void(const std::string &)> &cb);
  static void tail(const std::string &name, Data &buffer);

  //
  // Logger::Target
  //

  class Target {
  public:
    virtual ~Target() {}
    virtual void write(const Data &msg) = 0;
    virtual void shutdown() {}
  };

  //
  // Logger::StdoutTarget
  //

  class StdoutTarget : public Target {
  public:
    StdoutTarget(FILE *f) : m_f(f) {}

  private:
    virtual void write(const Data &msg) override;

    FILE* m_f;
    pjs::Ref<FileStream> m_file_stream;
  };

  //
  // Logger::FileTarget
  //

  class FileTarget : public Target {
  public:
    FileTarget(pjs::Str *filename);

  private:

    //
    // Logger::FileTarget::Module
    //

    class Module : public ModuleBase {
    public:
      Module() : ModuleBase("Logger::FileTarget") {}
      virtual auto new_context(pipy::Context *base) -> pipy::Context* override {
        return Context::make();
      }
    };

    virtual void write(const Data &msg) override;
    virtual void shutdown() override;

    pjs::Ref<Module> m_module;
    pjs::Ref<PipelineLayout> m_pipeline_layout;
    pjs::Ref<Pipeline> m_pipeline;
  };

  //
  // Logger::SyslogTarget
  //

  class SyslogTarget : public Target {
  public:
    enum class Priority {
      EMERG,
      ALERT,
      CRIT,
      ERR,
      WARNING,
      NOTICE,
      INFO,
      DEBUG,
    };

    SyslogTarget(Priority prority = Priority::INFO);

  private:
    virtual void write(const Data &msg) override;

    int m_priority;
  };

  //
  // Logger::HTTPTarget
  //

  class HTTPTarget : public Target {
  public:
    struct Options : public pipy::Options {
      size_t batch_size = 1000;
      int buffer_limit = 8*1024*1024;
      Pack::Options batch;
      tls::Client::Options tls;
      pjs::Ref<pjs::Str> method;
      pjs::Ref<pjs::Object> headers;

      Options() {}
      Options(pjs::Object *options);
    };

    HTTPTarget(pjs::Str *url, const Options &options);

  private:

    //
    // Logger::HTTPTarget::Module
    //

    class Module : public ModuleBase {
    public:
      Module() : ModuleBase("Logger::HTTPTarget") {}
      virtual auto new_context(pipy::Context *base) -> pipy::Context* override {
        return Context::make();
      }
    };

    pjs::Ref<Module> m_module;
    pjs::Ref<pjs::Method> m_mux_grouper;
    pjs::Ref<PipelineLayout> m_ppl;
    pjs::Ref<Pipeline> m_pipeline;
    pjs::Ref<MessageStart> m_message_start;

    virtual void write(const Data &msg) override;
    virtual void shutdown() override;
  };

  auto name() const -> pjs::Str* { return m_name; }

  void add_target(Target *target) {
    m_targets.push_back(std::unique_ptr<Target>(target));
  }

  void write(const Data &msg);

  virtual void log(int argc, const pjs::Value *args) = 0;

protected:
  Logger(pjs::Str *name);
  virtual ~Logger();

private:

  //
  // Logger::LogMessage
  //

  struct LogMessage :
    public pjs::Pooled<LogMessage>,
    public List<LogMessage>::Item
  {
    LogMessage(const Data &msg) : data(msg) {}
    Data data;
  };

  //
  // Logger::History
  //

  class History {
  public:
    static void write(const std::string &name, const Data &msg);
    static void tail(const std::string &name, Data &buffer);
    static void enable_streaming(const std::string &name, bool enabled);
    static void for_each(const std::function<void(History*)> &cb);

    auto name() const -> const std::string& { return m_name; }

  private:
    struct Message :
      public pjs::Pooled<Message>,
      public List<Message>::Item
    {
      Message(const Data &msg) : data(msg) {}
      Data data;
    };

    std::string m_name;
    List<LogMessage> m_messages;
    size_t m_size = 0;
    bool m_streaming_enabled = false;

    void write_message(const Data &msg);
    void dump_messages(Data &buffer);

    static std::map<std::string, History> s_all_histories;
  };

  pjs::Ref<pjs::Str> m_name;
  std::list<std::unique_ptr<Target>> m_targets;
  std::atomic<int> m_history_sending_size;

  void write_targets(const Data &msg);

  static AdminService* s_admin_service;
  static AdminLink* s_admin_link;
  static size_t s_history_limit;

  friend class pjs::ObjectTemplate<Logger>;
};

//
// BinaryLogger
//

class BinaryLogger : public pjs::ObjectTemplate<BinaryLogger, Logger> {
private:
  BinaryLogger(pjs::Str *name)
    : pjs::ObjectTemplate<BinaryLogger, Logger>(name) {}

  virtual void log(int argc, const pjs::Value *args) override;

  friend class pjs::ObjectTemplate<BinaryLogger, Logger>;
};

//
// TextLogger
//

class TextLogger : public pjs::ObjectTemplate<TextLogger, Logger> {
private:
  TextLogger(pjs::Str *name)
    : pjs::ObjectTemplate<TextLogger, Logger>(name) {}

  virtual void log(int argc, const pjs::Value *args) override;

  friend class pjs::ObjectTemplate<TextLogger, Logger>;
};

//
// JSONLogger
//

class JSONLogger : public pjs::ObjectTemplate<JSONLogger, Logger> {
private:
  JSONLogger(pjs::Str *name)
    : pjs::ObjectTemplate<JSONLogger, Logger>(name) {}

  virtual void log(int argc, const pjs::Value *args) override;

  friend class pjs::ObjectTemplate<JSONLogger, Logger>;
};

//
// Logging
//

class Logging : public pjs::ObjectTemplate<Logging>
{
};

} // namespace logging
} // namespace pipy

#endif // LOGGING_HPP
