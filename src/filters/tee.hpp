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

#ifndef TEE_HPP
#define TEE_HPP

#include "filter.hpp"
#include "file.hpp"
#include "data.hpp"
#include "options.hpp"
#include "fstream.hpp"

#include <atomic>
#include <string>

namespace pipy {

//
// Tee
//

class Tee : public Filter {
public:
  struct Options : public pipy::Options {
    bool shared = false;
    bool append = false;
    int max_file_size = 0;
    int max_file_count = 0;
    double rotate_interval = 0;
    Options() {}
    Options(pjs::Object *options);
  };

  class Target : public pjs::RefCountMT<Target> {
  public:
    Target(const std::string &filename, const Options &options);
    ~Target();

    void write(const Data &data);

  private:
    const std::string m_filename;
    Options m_options;
    pjs::Ref<File> m_file;
    int m_written_size = 0;
    double m_file_time = 0;

    void write_async(const Data &data);
  };

  Tee(const pjs::Value &filename, const Options &options);

private:
  Tee(const Tee &r);
  ~Tee();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  Options m_options;
  pjs::Value m_filename;
  pjs::Ref<pjs::Str> m_resolved_filename;
  pjs::Ref<File> m_file;
  pjs::Ref<Target> m_target;

  static std::map<std::string, pjs::Ref<Target>> s_targets;
  static std::mutex s_targets_mutex;

  static auto get_target(const std::string &filename, const Options &options) -> Target*;
};

} // namespace pipy

#endif // TEE_HPP
