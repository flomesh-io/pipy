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

#ifndef RESP_HPP
#define RESP_HPP

#include "filter.hpp"
#include "deframer.hpp"

namespace pipy {
namespace resp {

//
// Decoder
//

class Decoder : public Filter, public Deframer {
public:
  Decoder();

private:
  Decoder(const Decoder &r);
  ~Decoder();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  enum State {
    START,
    NEWLINE,
    SIMPLE_STRING,
    ERROR_STRING,
    BULK_STRING_SIZE,
    BULK_STRING_SIZE_NEWLINE,
    BULK_STRING_DATA,
    BULK_STRING_DATA_CR,
    INTEGER_START,
    INTEGER_POSITIVE,
    INTEGER_NEGATIVE,
    ARRAY_SIZE,
    ERROR,
  };

  struct Level : public pjs::Pooled<Level> {
    Level* back;
    pjs::Array *array;
    int index = 0;
  };

  pjs::Value m_root;
  Level* m_stack = nullptr;
  Data m_read_data;
  uint64_t m_read_int;

  virtual auto on_state(int state, int c) -> int override;

  void push_value(const pjs::Value &value);
  void message_start();
  void message_end();
};

//
// Encoder
//

class Encoder : public Filter {
};

} // namespace resp
} // namespace pipy

#endif // RESP_HPP
