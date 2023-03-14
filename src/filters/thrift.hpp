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

#ifndef THRIFT_HPP
#define THRIFT_HPP

#include "filter.hpp"
#include "api/thrift.hpp"

namespace pipy {
namespace thrift {

//
// Decoder
//

class Decoder : public Filter, public Thrift::Parser {
public:
  Decoder();

private:
  Decoder(const Decoder &r);
  ~Decoder();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  virtual void on_pass(Data &data) override;
  virtual void on_message_start() override;
  virtual void on_message_end(Thrift::Message *msg) override;
};

//
// Encoder
//

class Encoder : public Filter {
public:
  Encoder();

private:
  Encoder(const Encoder &r);
  ~Encoder();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

private:
  bool m_started = false;
  pjs::PropertyCache m_prop_seqID;
  pjs::PropertyCache m_prop_type;
  pjs::PropertyCache m_prop_name;
  pjs::PropertyCache m_prop_protocol;

  static void var_int(Data::Builder &db, uint64_t i);
};

} // namespace thrift
} // namespace pipy

#endif // THRIFT_HPP
