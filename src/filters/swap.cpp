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

#include "swap.hpp"
#include "api/pipeline-api.hpp"

namespace pipy {

//
// Swap
//

Swap::Swap(const pjs::Value &hub)
  : m_hub_value(hub)
  , m_buffer(Filter::buffer_stats())
{
}

Swap::Swap(const Swap &r)
  : Filter(r)
  , m_hub_value(r.m_hub_value)
  , m_buffer(r.m_buffer)
{
}

Swap::~Swap()
{
}

void Swap::dump(Dump &d) {
  Filter::dump(d);
  d.name = "swap";
}

auto Swap::clone() -> Filter* {
  return new Swap(*this);
}

void Swap::reset() {
  Filter::reset();
  m_buffer.clear();
  if (m_hub) {
    m_hub->exit(Filter::output());
    m_hub = nullptr;
  }
  m_is_started = false;
  m_is_outputting = false;
}

void Swap::process(Event *evt) {
  if (m_is_outputting) return;
  if (!m_is_started) {
    pjs::Value hub;
    if (!Filter::eval(m_hub_value, hub)) return;
    if (!hub.is_nullish()) {
      if (!hub.is<Hub>()) {
        Filter::error("callback did not return a Hub");
        return;
      }
      m_hub = hub.as<Hub>();
      m_hub->join(Filter::output());
      m_is_started = true;
      m_is_outputting = true;
      m_buffer.flush(
        [this](Event *evt) {
          m_hub->broadcast(evt, Filter::output());
        }
      );
      m_is_outputting = false;
    }
  }

  if (!m_is_started) {
    m_buffer.push(evt);

  } else if (auto h = m_hub.get()) {
    m_is_outputting = true;
    h->broadcast(evt, Filter::output());
    m_is_outputting = false;
  }
}

} // namespace pipy
