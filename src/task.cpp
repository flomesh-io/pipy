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

#include "task.hpp"
#include "listener.hpp"
#include "pipeline.hpp"
#include "input.hpp"
#include "worker.hpp"
#include "utils.hpp"

#include <signal.h>

std::map<std::string, int> s_signal_names = {
#ifdef _WIN32
  {"SIGINT", SIGINT},
  {"SIGILL", SIGILL},
  {"SIGFPE", SIGFPE},
  {"SIGSEGV", SIGSEGV},
  {"SIGTERM", SIGTERM},
  {"SIGBREAK", SIGBREAK},
  {"SIGABRT", SIGABRT},
#else
  { "SIGHUP"    , SIGHUP    },
  { "SIGINT"    , SIGINT    },
  { "SIGQUIT"   , SIGQUIT   },
  { "SIGILL"    , SIGILL    },
  { "SIGTRAP"   , SIGTRAP   },
  { "SIGABRT"   , SIGABRT   },
  { "SIGFPE"    , SIGFPE    },
  { "SIGKILL"   , SIGKILL   },
  { "SIGBUS"    , SIGBUS    },
  { "SIGSEGV"   , SIGSEGV   },
  { "SIGSYS"    , SIGSYS    },
  { "SIGPIPE"   , SIGPIPE   },
  { "SIGALRM"   , SIGALRM   },
  { "SIGTERM"   , SIGTERM   },
  { "SIGURG"    , SIGURG    },
  { "SIGSTOP"   , SIGSTOP   },
  { "SIGTSTP"   , SIGTSTP   },
  { "SIGCONT"   , SIGCONT   },
  { "SIGCHLD"   , SIGCHLD   },
  { "SIGTTIN"   , SIGTTIN   },
  { "SIGTTOU"   , SIGTTOU   },
  { "SIGIO"     , SIGIO     },
  { "SIGXCPU"   , SIGXCPU   },
  { "SIGXFSZ"   , SIGXFSZ   },
  { "SIGVTALRM" , SIGVTALRM },
  { "SIGPROF"   , SIGPROF   },
  { "SIGWINCH"  , SIGWINCH  },
#endif
};

namespace pipy {

Task::Task(const std::string &when, PipelineLayout *layout)
  : m_when(when)
  , m_signal_set(Net::context())
  , m_pipeline_layout(layout)
{
  if (when.empty()) {
    m_type = ONE_SHOT;
  } else {
    if (std::isdigit(when[0])) {
      m_type = CRON;
      m_interval = utils::get_seconds(when);
      if (m_interval < 0.01 || m_interval > 24 * 60 * 60) {
        std::string msg("task interval out of range: ");
        throw std::runtime_error(msg + when);
      }
    } else {
      auto i = s_signal_names.find(when);
      if (i == s_signal_names.end()) {
        std::string msg("invalid signal name: ");
        throw std::runtime_error(msg + when);
      }
      m_type = SIGNAL;
      m_signal = i->second;
      m_signal_set.add(i->second);
    }
  }
}

Task::~Task()
{
}

bool Task::active() const {
  return m_pipeline;
}

void Task::start() {
  switch (m_type) {
  case ONE_SHOT:
    run();
    break;
  case CRON:
    schedule(0);
    break;
  case SIGNAL:
    wait();
    break;
  }
}

void Task::end() {
  delete this;
}

void Task::schedule(double interval) {
  m_timer.schedule(
    interval,
    [this]() {
      run();
      schedule(m_interval);
    }
  );
}

void Task::wait() {
  m_signal_set.async_wait(
    [this](const std::error_code &ec, int) {
      if (!ec) run();
      wait();
    }
  );
}

void Task::run() {
  if (!active()) {
    InputContext ic;
    m_stream_end = false;
    m_pipeline = Pipeline::make(
      m_pipeline_layout,
      m_pipeline_layout->new_context()
    );
    m_pipeline->chain(EventTarget::input());
    m_pipeline->start();
  }
}

void Task::on_event(Event *evt) {
  if (evt->is<StreamEnd>()) {
    m_pipeline = nullptr;
    m_stream_end = true;
  }
}

} // namespace pipy
