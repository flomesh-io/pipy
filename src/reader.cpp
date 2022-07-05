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

#include "reader.hpp"
#include "file.hpp"
#include "fstream.hpp"
#include "pipeline.hpp"

namespace pipy {

//
// Reader
//

Reader::Reader(const std::string &pathname, PipelineLayout *layout)
  : m_pathname(pathname)
  , m_pipeline_layout(layout)
{
  auto *r = new FileReader(this, pathname);
  r->retain();
  m_readers.push(r);
}

Reader::~Reader() {
  while (auto r = m_readers.head()) {
    m_readers.remove(r);
    r->release();
  }
}

void Reader::start() {
  for (auto *r = m_readers.head(); r; r = r->next()) {
    r->start();
  }
}

//
// Reader::FileReader
//

Reader::FileReader::FileReader(Reader *reader, const std::string &pathname)
  : m_reader(reader)
  , m_file(File::make(pathname))
{
}

void Reader::FileReader::start() {
  m_file->open_read(
    [this](FileStream *fs) {
      if (fs) {
        auto ppl = m_reader->m_pipeline_layout.get();
        auto p = Pipeline::make(ppl, ppl->new_context());
        fs->chain(EventTarget::input());
        m_pipeline = p;
        m_stream = fs;
        p->start();
      }
      release();
    }
  );

  retain();
}

void Reader::FileReader::on_event(Event *evt) {
  auto is_end = evt->is<StreamEnd>();
  if (m_pipeline) {
    m_pipeline->input()->input(evt);
    if (is_end) m_pipeline = nullptr;
  }
}

} // namespace pipy
