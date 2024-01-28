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

#include "fcgi.hpp"

namespace pipy {
namespace fcgi {

//
// Demux
//

Demux::Demux()
{
}

Demux::Demux(const Demux &r) {
}

Demux::~Demux() {
}

void Demux::dump(Dump &d) {
  Filter::dump(d);
  d.name = "demuxFastCGI";
  d.sub_type = Dump::DEMUX;
}

auto Demux::clone() -> Filter* {
  return new Demux(*this);
}

void Demux::reset() {
  Filter::reset();
}

void Demux::process(Event *evt) {
}

void Demux::shutdown() {
}

auto Demux::on_demux_open_stream() -> EventFunction* {
  auto p = Filter::sub_pipeline(0, true);
  p->retain();
  p->start();
  return p;
}

void Demux::on_demux_close_stream(EventFunction *stream) {
  auto p = static_cast<Pipeline*>(stream);
  p->release();
}

void Demux::on_demux_complete() {
  if (auto eos = m_eos) {
    Filter::output(eos);
    m_eos = nullptr;
  }
}

//
// Mux
//

Mux::Mux()
{
}

Mux::Mux(pjs::Function *session_selector)
  : MuxBase(session_selector)
{
}

Mux::Mux(pjs::Function *session_selector, const MuxSession::Options &options)
  : MuxBase(session_selector)
  , m_options(options)
{
}

Mux::Mux(pjs::Function *session_selector, pjs::Function *options)
  : MuxBase(session_selector, options)
{
}

Mux::Mux(const Mux &r) {
}

void Mux::dump(Dump &d) {
  Filter::dump(d);
  d.name = "muxFastCGI";
  d.sub_type = Dump::MUX;
}

auto Mux::clone() -> Filter* {
  return new Mux(*this);
}

auto Mux::on_mux_new_pool(pjs::Object *options) -> MuxSessionPool* {
  return new SessionPool(options);
}

//
// Mux::Session
//

void Mux::Session::mux_session_open(MuxSource *source) {
}

auto Mux::Session::mux_session_open_stream(MuxSource *source) -> EventFunction* {
  return nullptr;
}

void Mux::Session::mux_session_close_stream(EventFunction *stream) {
}

void Mux::Session::mux_session_close() {
}

} // namespace fcgi
} // namespace pipy

namespace pjs {

using namespace pipy;
using namespace pipy::fcgi;

template<> void ClassDef<MessageHead>::init() {
  field<int>("version", [](MessageHead *obj) { return &obj->version; });
  field<int>("requestID", [](MessageHead *obj) { return &obj->requestID; });
}

template<> void ClassDef<RequestHead>::init() {
  super<MessageHead>();
  field<int>("role", [](RequestHead *obj) { return &obj->role; });
  field<int>("flags", [](RequestHead *obj) { return &obj->flags; });
  field<Ref<Array>>("params", [](RequestHead *obj) { return &obj->params; });
}

template<> void ClassDef<ResponseHead>::init() {
  super<MessageHead>();
  field<int>("appStatus", [](ResponseHead *obj) { return &obj->appStatus; });
  field<int>("protocolStatus", [](ResponseHead *obj) { return &obj->protocolStatus; });
}

template<> void ClassDef<ResponseTail>::init() {
  field<Ref<pipy::Data>>("params", [](ResponseTail *obj) { return &obj->stderr_data; });
}

} // namespace pjs
