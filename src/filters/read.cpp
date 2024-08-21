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

#include "read.hpp"
#include "file.hpp"
#include "fstream.hpp"
#include "input.hpp"

namespace pipy {

//
// Read::Options
//

Read::Options::Options(pjs::Object *options) {
  Value(options, "seek")
    .get(seek)
    .get(seek_f)
    .check_nullable();
  Value(options, "size")
    .get(size)
    .get(size_f)
    .check_nullable();
}

//
// Read
//

Read::Read(const pjs::Value &pathname, const Options &options)
  : m_pathname(pathname)
  , m_options(options)
{
}

Read::Read(const Read &r)
  : m_pathname(r.m_pathname)
  , m_options(r.m_options)
{
}

Read::~Read()
{
}

void Read::dump(Dump &d) {
  Filter::dump(d);
  d.name = "read";
}

auto Read::clone() -> Filter* {
  return new Read(*this);
}

void Read::reset() {
  Filter::reset();
  EventSource::close();
  if (m_file) {
    m_file->close();
    m_file = nullptr;
  }
  m_started = false;
}

void Read::process(Event *evt) {
  if (!m_started) {
    m_started = true;
    pjs::Value pathname;
    if (!Filter::eval(m_pathname, pathname)) return;

    int seek = m_options.seek;
    if (auto f = m_options.seek_f.get()) {
      pjs::Value ret;
      if (!Filter::callback(f, 0, nullptr, ret)) return;
      seek = ret.to_int32();
    }

    int size = m_options.size;
    if (auto f = m_options.size_f.get()) {
      pjs::Value ret;
      if (!Filter::callback(f, 0, nullptr, ret)) return;
      size = ret.to_int32();
    }

    auto *s = pathname.to_string();
    auto f = File::make(s->str());
    m_file = f->retain();
    m_file->open_read(
      seek, size,
      [=](FileStream *fs) {
        if (fs) {
          fs->chain(EventSource::reply());
        } else if (m_file == f) {
          InputContext ic;
          Filter::error("unable to open file for reading: %s", s->c_str());
        }
        f->release();
        s->release();
      }
    );
  }
}

void Read::on_reply(Event *evt) {
  Filter::output(evt);
}

} // namespace pipy
