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

#ifndef READER_HPP
#define READER_HPP

#include "pjs/pjs.hpp"
#include "list.hpp"
#include "event.hpp"

#include <string>

namespace pipy {

class File;
class FileStream;
class PipelineLayout;

//
// Reader
//

class Reader {
public:
  static auto make(const std::string &pathname, PipelineLayout *layout) -> Reader* {
    return new Reader(pathname, layout);
  }

  void start();

private:
  Reader(const std::string &pathname, PipelineLayout *layout);
  ~Reader();

  //
  // Reader::FileReader
  //

  class FileReader :
    public pjs::RefCount<FileReader>,
    public pjs::Pooled<FileReader>,
    public List<FileReader>::Item,
    public EventTarget
  {
  public:
    FileReader(Reader *reader, const std::string &pathname);

    void start();

  private:
    virtual void on_event(Event *evt) override;

    Reader* m_reader;
    pjs::Ref<File> m_file;
    pjs::Ref<FileStream> m_stream;
    pjs::Ref<Pipeline> m_pipeline;
  };

  std::string m_pathname;
  pjs::Ref<PipelineLayout> m_pipeline_layout;
  List<FileReader> m_readers;

  friend class Worker;
};

} // namespace pipy

#endif // READER_HPP
