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

#include <list>
#include <memory>

namespace pipy {

class AdminLink;
class Data;
class Pipeline;
class PipelineLayout;

namespace logging {

//
// Logger
//

class Logger : public pjs::ObjectTemplate<Logger> {
public:

  //
  // Logger::Target
  //

  class Target {
  public:
    virtual void write(const Data &msg) = 0;
  };

  //
  // Logger::AdminTarget
  //

  class AdminTarget : public Target {
  public:
    AdminTarget(AdminLink *admin_link);

  private:
    virtual void write(const Data &msg) override;

    pjs::Ref<PipelineLayout> m_layout;
    pjs::Ref<Pipeline> m_pipeline;
  };

  //
  // Logger::FileTarget
  //

  class FileTarget : public Target {
  public:
    FileTarget(pjs::Str *filename);

  private:
    virtual void write(const Data &msg) override;

    pjs::Ref<PipelineLayout> m_pipeline_layout;
    pjs::Ref<Pipeline> m_pipeline;
  };

  //
  // Logger::HTTPTarget
  //

  class HTTPTarget : public Target {
  public:
    struct Options : public pipy::Options {
      size_t size = 1000;
      double interval = 5;
      std::string head;
      std::string tail;
      std::string separator;
      pjs::Ref<pjs::Str> method;
      pjs::Ref<pjs::Object> headers;

      Options() {}
      Options(pjs::Object *options);
    };

    HTTPTarget(pjs::Str *url, const Options &options);

  private:
    pjs::Ref<PipelineLayout> m_layout;
    pjs::Ref<Pipeline> m_pipeline;

    virtual void write(const Data &msg) override;
  };

  void add_target(Target *target) {
    m_targets.push_back(std::unique_ptr<Target>(target));
  }

  virtual void log(int argc, const pjs::Value *args) = 0;

protected:
  Logger(pjs::Str *name);

  void write(const Data &msg);

private:
  pjs::Ref<pjs::Str> m_name;
  std::list<std::unique_ptr<Target>> m_targets;

  friend class pjs::ObjectTemplate<Logger>;
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
