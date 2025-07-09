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

#ifndef DETECT_PROTOCOL_HPP
#define DETECT_PROTOCOL_HPP

#include "filter.hpp"
#include "timer.hpp"
#include "options.hpp"

namespace pipy {

class ProtocolDetector : public Filter {
public:
  struct Options : public pipy::Options {
    double timeout = 1;
    Options() {}
    Options(pjs::Object *options);
  };

  ProtocolDetector(pjs::Function *callback, const Options &options);

  class Detector {
  public:
    virtual ~Detector() {};
    virtual auto feed(const char *data, size_t size) -> pjs::Str* = 0;
  };

private:
  enum { MAX_DETECTORS = 3 };

  ProtocolDetector(const ProtocolDetector &r);
  ~ProtocolDetector();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  pjs::Ref<pjs::Function> m_callback;
  Options m_options;
  Timer m_timer;
  Detector* m_detectors[MAX_DETECTORS];
  int m_num_detectors = 0;
  int m_negatives = 0;
  pjs::Ref<pjs::Str> m_result;

  void done();
};

} // namespace pipy

#endif // DETECT_PROTOCOL_HPP
