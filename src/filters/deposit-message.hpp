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

#ifndef DEPOSIT_MESSAGE_HPP
#define DEPOSIT_MESSAGE_HPP

#include "filter.hpp"
#include "file.hpp"
#include "data.hpp"
#include "fstream.hpp"

namespace pipy {

//
// DepositMessageReceiver
//

class DepositMessageReceiver : public EventTarget {
  virtual void on_event(Event *evt) override;
};

//
// DepositMessage
//

class DepositMessage : public Filter, public DepositMessageReceiver {
public:
  struct Options {
    size_t threshold = 0;
    bool keep = false;
  };

  DepositMessage(const pjs::Value &filename, const Options &options);

private:
  DepositMessage(const DepositMessage &r);
  ~DepositMessage();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(std::ostream &out) override;

  pjs::Value m_filename;
  Options m_options;
  pjs::Ref<pjs::Str> m_resolved_filename;
  pjs::Ref<File> m_file_w;
  pjs::Ref<File> m_file_r;
  pjs::Ref<Event> m_end;
  Data m_buffer;
  bool m_started = false;

  void open();
  void close();
  void end();

  friend class DepositMessageReceiver;
};

} // namespace pipy

#endif // DEPOSIT_MESSAGE_HPP
