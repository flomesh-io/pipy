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
#include "api/url.hpp"

#include <string>

namespace pipy {

//
// AdminLink
//

class AdminLink {
public:
  AdminLink(
    const std::string &url,
    const std::function<void(const Data&)> &on_receive
  );

  auto connect() -> int;
  void send(const Data &data);

private:

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
    virtual void dump(std::ostream &out) override;

    AdminLink* m_admin_link;
    Data m_payload;
    bool m_started = false;
  };

  pjs::Ref<URL> m_url;
  std::function<void(const Data&)> m_on_receive;
  pjs::Ref<Pipeline> m_pipeline;
  pjs::Ref<PipelineDef> m_pipeline_def;
  pjs::Ref<PipelineDef> m_pipeline_def_tunnel;
  pjs::Ref<PipelineDef> m_pipeline_def_connect;
  pjs::Ref<Message> m_handshake;
  int m_connection_id = 0;
};

} // namespace pipy

#endif // ADMIN_LINK_HPP
