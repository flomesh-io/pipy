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

namespace pipy {

//
// OnBody
//

OnBody::OnBody(pjs::Function *callback, const DataBuffer::Options &options)
  : Handle(callback)
  , m_body_buffer(options, Filter::buffer_stats())
{
}

OnBody::OnBody(const OnBody &r)
  : Handle(r)
  , m_body_buffer(r.m_body_buffer)
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
  Handle::reset();
  m_started = false;
  m_body_buffer.clear();
}

void OnBody::handle(Event *evt) {
  if (evt->is<MessageStart>()) {
    m_started = true;

  } else if (auto data = evt->as<Data>()) {
    if (m_started) {
      m_body_buffer.push(*data);
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (m_started) {
      auto body = m_body_buffer.flush();
      m_started = false;
      if (Handle::callback(body)) {
        Handle::defer(evt);
      }
      return;
    }
  }

  Handle::pass(evt);
}

} // namespace pipy
