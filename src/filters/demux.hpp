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

#ifndef DEMUX_HPP
#define DEMUX_HPP

#include "filter.hpp"
#include "buffer.hpp"
#include "session.hpp"

#include <queue>

namespace pipy {

//
// Demux
//

class Demux : public Filter {
public:
  Demux();
  Demux(pjs::Str *target);

private:
  Demux(const Demux &r);
  ~Demux();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto draw(std::list<std::string> &links, bool &fork) -> std::string override;
  virtual void bind() override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  struct Channel : public pjs::Pooled<Channel> {
    pjs::Ref<Session> session;
    EventBuffer buffer;
    bool input_end = false;
    bool output_end = false;
    ~Channel() { session->on_output(nullptr); }
  };

  Pipeline* m_pipeline = nullptr;
  pjs::Ref<pjs::Str> m_target;
  std::queue<Channel*> m_queue;
  bool m_session_end = false;

  bool flush();
};

} // namespace pipy

#endif // DEMUX_HPP