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

#include "worker-thread.hpp"
#include "worker.hpp"
#include "codebase.hpp"

#include <mutex>

namespace pipy {

WorkerThread::WorkerThread(int index)
  : m_index(index)
{
}

bool WorkerThread::start() {
  m_thread = std::thread(
    [this]() {
      auto &entry = Codebase::current()->entry();
      auto worker = Worker::make();
      auto mod = worker->load_js_module(entry);

      if (!mod) {
        fail();
        return;
      }

      if (!worker->start()) {
        fail();
        return;
      }

      std::lock_guard<std::mutex> lock(m_mutex);
      m_started = true;
    }
  );

  std::unique_lock<std::mutex> lock(m_mutex);
  m_cv.wait(lock, [this]() { return m_started || m_failed; });

  return !m_failed;
}

void WorkerThread::reload() {
  m_io_context.post(
    []() {
      Worker::restart();
    }
  );
}

auto WorkerThread::stop(bool force) -> int {
  if (force) {
    m_io_context.stop();
    m_thread.join();
    return 0;

  } else {
    if (!m_shutdown) {
      m_io_context.post(
        []() {
          if (auto worker = Worker::current()) worker->stop();
          Listener::for_each([&](Listener *l) { l->pipeline_layout(nullptr); });
        }
      );
      m_shutdown = true;
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    m_polled = false;

    m_io_context.post(
      [this]() {
        int n = 0;
        PipelineLayout::for_each(
          [&](PipelineLayout *layout) {
            n += layout->active();
          }
        );
        m_pending_pipelines = n;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_polled = true;
      }
    );

    m_cv.wait(lock, [this]() { return m_polled; });
    if (m_pending_pipelines > 0) return m_pending_pipelines;

    m_io_context.stop();
    m_thread.join();

    return 0;
  }
}

void WorkerThread::fail() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_failed = true;
}

} // namespace pipy
