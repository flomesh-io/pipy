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

#include "on-body.hpp"
#include "log.hpp"

namespace pipy {

//
// OnBody
//

OnBody::OnBody(pjs::Function *callback, int size_limit)
  : m_callback(callback)
  , m_size_limit(size_limit)
{
}

OnBody::OnBody(const OnBody &r)
  : Filter(r)
  , m_callback(r.m_callback)
  , m_size_limit(r.m_size_limit)
{
}

OnBody::~OnBody()
{
}

void OnBody::dump(Dump &d) {
  Filter::dump(d);
  d.name = "handleMessageBody";
}

auto OnBody::clone() -> Filter* {
  return new OnBody(*this);
}

void OnBody::reset() {
  Filter::reset();
  m_body = nullptr;
  m_discarded_size = 0;
}

void OnBody::process(Event *evt) {
  if (evt->is<MessageStart>()) {
    m_body = Data::make();

  } else if (auto data = evt->as<Data>()) {
    if (m_body && data->size() > 0) {
      if (m_size_limit >= 0) {
        auto room = m_size_limit - m_body->size();
        if (room >= data->size()) {
          m_body->push(*data);
        } else if (room > 0) {
          Data buf(*data);
          auto discard = buf.size() - room;
          buf.pop(discard);
          m_body->push(buf);
          m_discarded_size += discard;
        } else {
          m_discarded_size += data->size();
        }
      } else {
        m_body->push(*data);
      }
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (m_body) {
      if (m_discarded_size > 0 && m_size_limit > 0) {
        Log::error(
          "[handleMessageBody] %d bytes were discarded due to buffer size limit of %d",
          m_discarded_size, m_size_limit
        );
      }
      pjs::Value arg(m_body), result;
      if (!callback(m_callback, 1, &arg, result)) return;
      m_body = nullptr;
      m_discarded_size = 0;
    }
  }

  output(evt);
}

} // namespace pipy
