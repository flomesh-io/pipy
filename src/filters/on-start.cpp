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

#include "on-start.hpp"

namespace pipy {

OnStart::OnStart()
{
}

OnStart::OnStart(pjs::Function *callback)
  : m_callback(callback)
{
}

OnStart::OnStart(const OnStart &r)
  : m_callback(r.m_callback)
{
}

OnStart::~OnStart()
{
}

auto OnStart::help() -> std::list<std::string> {
  return {
    "handleSessionStart(callback)",
    "Handles the initial event in a session",
    "callback = <function> Callback function that receives the initial event",
  };
}

void OnStart::dump(std::ostream &out) {
  out << "handleSessionStart";
}

auto OnStart::clone() -> Filter* {
  return new OnStart(*this);
}

void OnStart::reset() {
  m_started = false;
}

void OnStart::process(Context *ctx, Event *inp) {
  if (!m_started) {
    m_started = true;
    pjs::Value arg(inp), result;
    if (!callback(*ctx, m_callback, 1, &arg, result)) return;
  }

  output(inp);
}

} // namespace pipy