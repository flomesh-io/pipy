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
  Merge(pjs::Function *group, pjs::Object *options);

private:
  Merge(const Merge &r);
  ~Merge();

  virtual auto clone() -> Filter* override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;
  virtual auto on_new_cluster(pjs::Object *options) -> MuxBase::SessionCluster* override;

  //
  // Merge::Session
  //

  class Session : public pjs::Pooled<Session, MuxBase::Session> {
    virtual auto open_stream() -> EventFunction* override;
    virtual void close_stream(EventFunction *stream) override;

    friend class Stream;
  };

  //
  // Merge::SessionCluster
  //

  class SessionCluster : public pjs::Pooled<SessionCluster, MuxBase::SessionCluster> {
    using pjs::Pooled<SessionCluster, MuxBase::SessionCluster>::Pooled;

    virtual auto session() -> MuxBase::Session* override { return new Session(); }
    virtual void free() override { delete this; }

    friend class Merge;
  };

  //
  // Merge::Stream
  //

  class Stream :
    public pjs::Pooled<Stream>,
    public EventFunction
  {
    Stream(Session *session)
      : m_output(session->input()) {}

    virtual void on_event(Event *evt) override;

    pjs::Ref<Input> m_output;
    pjs::Ref<MessageStart> m_start;
    Data m_buffer;

    friend class Session;
  };
};

} // namespace pipy

#endif // MERGE_HPP
