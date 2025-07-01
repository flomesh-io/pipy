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

#ifndef CONNECT_HPP
#define CONNECT_HPP

#include "filter.hpp"
#include "outbound.hpp"
#include "options.hpp"

namespace pipy {

//
// Connect
//

class Connect : public Filter {
public:
  struct Options : public pipy::Options, public Outbound::Options {
    pjs::Ref<pjs::Str> bind;
    pjs::Ref<Data> bind_d;
    pjs::Ref<pjs::Function> bind_f;
    pjs::Ref<pjs::Function> on_state_f;
    Options() {}
    Options(const Outbound::Options &options) : Outbound::Options(options) {}
    Options(pjs::Object *options);
  };

  Connect(const pjs::Value &target, const Options &options);
  Connect(const pjs::Value &target, pjs::Function *options);

private:
  Connect(const Connect &r);
  ~Connect();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  pjs::Value m_target;
  pjs::Ref<Outbound> m_outbound;
  pjs::Ref<pjs::Function> m_options_f;
  Options m_options;
  bool m_end_input = false;

  friend class ConnectReceiver;
};

} // namespace pipy

#endif // CONNECT_HPP
