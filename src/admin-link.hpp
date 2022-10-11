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

#ifndef ADMIN_LINK_HPP
#define ADMIN_LINK_HPP

#include "filter.hpp"
#include "data.hpp"
#include "message.hpp"
#include "module.hpp"
#include "pipeline.hpp"
#include "api/url.hpp"

#include <string>
#include <list>

namespace pipy {

//
// AdminLink
//

class AdminLink {
public:
  typedef std::function<bool(const std::string &, const Data &)> Handler;

  AdminLink(const std::string &url);

  auto connect() -> int;
  void add_handler(const Handler &handler);
  void send(const Data &data);
  void close();

private:

  //
  // AdminLink::Module
  //

  class Module : public ModuleBase {
  public:
    Module() : ModuleBase("AdminLink") {}
    virtual auto new_context(pipy::Context *base) -> pipy::Context* override {
      return new Context();
    }
  };

  //
  // AdminLink::Receiver
  //

  class Receiver : public Filter {
  public:
    Receiver(AdminLink *admin_link) : m_admin_link(admin_link) {}

  private:
    virtual auto clone() -> Filter* override;
    virtual void reset() override;
    virtual void process(Event *evt) override;
    virtual void dump(Dump &d) override;

    AdminLink* m_admin_link;
    Data m_payload;
    bool m_started = false;
  };

  pjs::Ref<Module> m_module;
  pjs::Ref<URL> m_url;
  pjs::Ref<Pipeline> m_pipeline;
  pjs::Ref<PipelineLayout> m_ppl;
  pjs::Ref<Message> m_handshake;
  std::list<Handler> m_handlers;
  int m_connection_id = 0;
};

} // namespace pipy

#endif // ADMIN_LINK_HPP
