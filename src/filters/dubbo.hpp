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

#ifndef DUBBO_HPP
#define DUBBO_HPP

#include "filter.hpp"
#include "buffer.hpp"
#include "data.hpp"

namespace pipy {
namespace dubbo {

//
// Decoder
//

class Decoder : public Filter {
public:
  Decoder();

private:
  Decoder(const Decoder &r);
  ~Decoder();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

private:
  enum State {
    FRAME_HEAD,
    FRAME_DATA,
  };

  State m_state;
  int m_size;
  ByteBuf<16> m_head;
  pjs::Ref<pjs::Object> m_head_object;

  std::string m_var_request_id;
  std::string m_var_request_bit;
  std::string m_var_2_way_bit;
  std::string m_var_event_bit;
  std::string m_var_status;
  bool m_session_end = false;
};

//
// Encoder
//

class Encoder : public Filter {
public:
  Encoder();
  Encoder(pjs::Object *head);

private:
  Encoder(const Encoder &r);
  ~Encoder();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

private:
  pjs::Ref<Data> m_buffer;
  pjs::Ref<MessageStart> m_message_start;
  pjs::Ref<pjs::Object> m_head_object;
  pjs::PropertyCache m_prop_id;
  pjs::PropertyCache m_prop_status;
  pjs::PropertyCache m_prop_is_request;
  pjs::PropertyCache m_prop_is_two_way;
  pjs::PropertyCache m_prop_is_event;
  long long m_auto_id = 0;
  bool m_session_end = false;

  static long long get_header(
    const Context &ctx,
    pjs::Object *obj,
    pjs::PropertyCache &prop,
    long long value
  );
};

} // namespace dubbo
} // namespace pipy

#endif // DUBBO_HPP