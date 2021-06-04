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

#include "demux.hpp"
#include "context.hpp"
#include "pipeline.hpp"
#include "module.hpp"
#include "logging.hpp"

namespace pipy {

//
// Demux
//

Demux::Demux()
{
}

Demux::Demux(pjs::Str *target)
  : m_target(target)
{
}

Demux::Demux(const Demux &r)
  : Demux(r.m_target)
{
}

Demux::~Demux()
{
}

auto Demux::help() -> std::list<std::string> {
  return {
    "demux(target)",
    "Sends messages to a different pipline with each one in its own session and context",
    "target = <string> Name of the pipeline to send messages to",
  };
}

void Demux::dump(std::ostream &out) {
  out << "demux";
}

auto Demux::draw(std::list<std::string> &links, bool &fork) -> std::string {
  links.push_back(m_target->str());
  fork = false;
  return "demux";
}

auto Demux::clone() -> Filter* {
  return new Demux(*this);
}

void Demux::reset() {
  while (!m_queue.empty()) {
    auto channel = m_queue.front();
    m_queue.pop();
    delete channel;
  }
  m_session_end = false;
}

void Demux::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (inp->is<MessageStart>()) {
    auto mod = pipeline()->module();
    auto p = mod->find_named_pipeline(m_target);
    if (!p) {
      Log::error("[demux] unknown pipeline: %s", m_target->c_str());
      abort();
      return;
    }
    auto channel = new Channel;
    auto context = mod->worker()->new_runtime_context(ctx);
    auto session = Session::make(context, p);
    session->on_output([=](Event *inp) {
      if (channel->output_end) return;
      if (inp->is<SessionEnd>()) {
        channel->output_end = true;
      } else {
        channel->buffer.push(inp);
        if (inp->is<MessageEnd>()) {
          channel->output_end = true;
        }
      }
      flush();
    });
    channel->session = session;
    m_queue.push(channel);
    session->input(inp);

  } else if (inp->is<SessionEnd>()) {
    m_session_end = true;

  } else if (!m_queue.empty()) {
    auto channel = m_queue.back();
    if (!channel->input_end) {
      channel->session->input(inp);
      if (inp->is<MessageEnd>()) {
        channel->input_end = true;
      }
    }
  }
}

bool Demux::flush() {
  bool has_sent = false;

  // Flush all completed sessions in the front
  while (!m_queue.empty()) {
    auto channel = m_queue.front();
    if (!channel->output_end) break;
    m_queue.pop();
    auto &buffer = channel->buffer;
    if (!buffer.empty()) {
      buffer.flush(out());
      has_sent = true;
    }
    delete channel;
  }

  // Flush the first session in queue
  if (!m_queue.empty()) {
    auto channel = m_queue.front();
    auto &buffer = channel->buffer;
    if (!buffer.empty()) {
      buffer.flush(out());
      has_sent = true;
    }
  }
  return has_sent;
}

} // namespace pipy