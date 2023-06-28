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

#ifndef ON_HANDLE_HPP
#define ON_HANDLE_HPP

#include "filter.hpp"

namespace pipy {

//
// Handle
//

class Handle : public Filter {
public:

  //
  // Handle::PromiseCallback
  //

  class PromiseCallback : public pjs::ObjectTemplate<PromiseCallback, pjs::Promise::Callback> {
    PromiseCallback(Handle *filter) : m_filter(filter) {}
    virtual void on_resolved(const pjs::Value &value) override;
    virtual void on_rejected(const pjs::Value &error) override;
    friend class pjs::ObjectTemplate<PromiseCallback, pjs::Promise::Callback>;
    Handle* m_filter;
  public:
    void close() { m_filter = nullptr; }
  };

  Handle(pjs::Function *callback);

protected:
  Handle(const Handle &r);
  ~Handle();

  virtual void handle(Event *evt) {}
  virtual bool on_callback_return(const pjs::Value &result);

  bool callback(pjs::Object *arg);
  void defer(Event *evt);
  void pass(Event *evt);

  virtual void reset() override;
  virtual void process(Event *evt) override;

private:
  pjs::Ref<pjs::Function> m_callback;
  pjs::Ref<pjs::Promise> m_promise;
  pjs::Ref<PromiseCallback> m_promise_callback;
  pjs::Ref<Event> m_deferred_event;
  EventBuffer m_event_buffer;
  bool m_waiting = false;
};

} // namespace pipy

#endif // ON_HANDLE_HPP
