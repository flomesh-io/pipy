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

#ifndef BGP_HPP
#define BGP_HPP

#include "filter.hpp"
#include "api/bgp.hpp"
#include "options.hpp"

namespace pipy {
namespace bgp {

//
// Options
//

struct Options : public pipy::Options {
  bool enable_as4 = false;
  pjs::Ref<pjs::Function> enable_as4_f;
  Options() {}
  Options(pjs::Object *options);
};

//
// Decoder
//

class Decoder : public Filter, public BGP::Parser {
public:
  Decoder();
  Decoder(const Options &options);

private:
  Decoder(const Decoder &r);
  ~Decoder();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  virtual void on_pass(Data &data) override;
  virtual void on_parse_start() override;
  virtual void on_message_start() override;
  virtual void on_message_end(pjs::Object *payload) override;

  Options m_options;
};

//
// Encoder
//

class Encoder : public Filter {
public:
  Encoder();
  Encoder(const Options &options);

private:
  Encoder(const Encoder &r);
  ~Encoder();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  Options m_options;
  bool m_message_started = false;
};

} // namespace bgp
} // namespace pipy

#endif // BGP_HPP
