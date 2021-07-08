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

#ifndef PACK_HPP
#define PACK_HPP

#include "filter.hpp"
#include "data.hpp"
#include "timer.hpp"

#include <chrono>
#include <memory>

namespace pipy {

//
// Pack
//

class Pack : public Filter {
public:
  Pack();
  Pack(int batch_size, pjs::Object *options);

private:
  Pack(const Pack &r);
  ~Pack();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  int m_batch_size;
  int m_message_starts = 0;
  int m_message_ends = 0;
  double m_timeout = 0;
  double m_vacancy = 0.5;
  pjs::Ref<pjs::Object> m_mctx;
  pjs::Ref<pjs::Object> m_head;
  pjs::Ref<Data> m_buffer;
  std::unique_ptr<Timer> m_timer;
  std::chrono::steady_clock::time_point m_last_input_time;
  bool m_session_end = false;

  void flush(MessageEnd *end);
  void schedule_timeout();
  void check_timeout();
};

} // namespace pipy

#endif // PACK_HPP