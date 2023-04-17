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

#ifndef REPLACE_BODY_HPP
#define REPLACE_BODY_HPP

#include "replace.hpp"
#include "buffer.hpp"

namespace pipy {

//
// ReplaceBody
//

class ReplaceBody : public Replace {
public:
  ReplaceBody(pjs::Object *replacement, const Buffer::Options &options);

private:
  ReplaceBody(const ReplaceBody &r);
  ~ReplaceBody();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void dump(Dump &d) override;
  virtual void handle(Event *evt) override;

  Buffer m_body_buffer;
  bool m_started = false;
};

} // namespace pipy

#endif // REPLACE_BODY_HPP
