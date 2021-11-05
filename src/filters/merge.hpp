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

#ifndef MERGE_HPP
#define MERGE_HPP

#include "mux.hpp"

namespace pipy {

//
// Merge
//

class Merge : public MuxBase {
public:
  Merge(const pjs::Value &key);

private:
  Merge(const Merge &r);
  ~Merge();

  virtual auto clone() -> Filter* override;
  virtual void dump(std::ostream &out) override;

private:

  //
  // Merge::Session
  //

  class Session :
    public pjs::Pooled<Session>,
    public MuxBase::Session
  {

    //
    // Merge::Session::Stream
    //

    class Stream :
      public pjs::Pooled<Stream>,
      public MuxBase::Session::Stream
    {
      Stream(Session *session, MessageStart *start)
        : m_session(session)
        , m_start(start) {}

      virtual void on_event(Event *evt) override;
      virtual void close() override;

      Session* m_session;
      pjs::Ref<MessageStart> m_start;
      Data m_buffer;

      friend class Session;
    };

    virtual void open(Pipeline *pipeline) override;
    virtual auto stream(MessageStart *start) -> Stream* override;
    virtual void on_demux(Event *evt) override;

    friend class Stream;
  };

  virtual auto new_session() -> Session* override {
    return new Session();
  }
};

} // namespace pipy

#endif // MERGE_HPP
