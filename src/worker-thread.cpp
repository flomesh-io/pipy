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
#include "net.hpp"

namespace pipy {

WorkerThread::WorkerThread(int index)
  : m_index(index)
{
}

WorkerThread::~WorkerThread() {
  m_thread.join();
}

bool WorkerThread::start() {
  std::unique_lock<std::mutex> lock(m_mutex);

  m_thread = std::thread(
    [this]() {
      auto &entry = Codebase::current()->entry();
      auto worker = Worker::make();
      auto mod = worker->load_js_module(entry);
      bool started = (mod && worker->start());

      {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_started = started;
        m_failed = !started;
        m_net = &Net::current();
      }

      m_cv.notify_one();

      if (started) {
        Net::current().run();
      }
    }
  );

  m_cv.wait(lock, [this]() { return m_started || m_failed; });
  return !m_failed;
}

void WorkerThread::stats(const std::function<void(stats::MetricData&)> &cb) {
  m_net->post(
    [=]() {
      stats::Metric::local().collect_all();
      m_metric_data.update(stats::Metric::local());
      cb(m_metric_data);
    }
  );
}

void WorkerThread::reload() {
  m_net->post(
    []() {
      Worker::restart();
    }
  );
}

auto WorkerThread::stop(bool force) -> int {
  if (force) {
    m_net->stop();
    m_thread.join();
    return 0;

  } else if (!m_shutdown) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_shutdown = true;
    m_pending_pipelines = -1;

    m_net->post(
      [this]() {
        if (auto worker = Worker::current()) worker->stop();
        Listener::for_each([&](Listener *l) { l->pipeline_layout(nullptr); });
        m_pending_timer = new Timer();
        wait();
      }
    );

    m_cv.wait(lock, [this]() { return m_pending_pipelines >= 0; });
    return m_pending_pipelines;

  } else {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pending_pipelines;
  }
}

void WorkerThread::wait() {
  int n = 0;
  PipelineLayout::for_each(
    [&](PipelineLayout *layout) {
      n += layout->active();
    }
  );

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending_pipelines = n;
  }
  m_cv.notify_one();

  if (n > 0) {
    m_pending_timer->schedule(1, [this]() { wait(); });
  } else {
    delete m_pending_timer;
    m_pending_timer = nullptr;
    m_net->stop();
  }
}

void WorkerThread::fail() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_failed = true;
}

//
// WorkerManager
//

auto WorkerManager::get() -> WorkerManager& {
  static WorkerManager s_worker_manager;
  return s_worker_manager;
}

bool WorkerManager::start() {
  if (m_worker_thread) return false;

  auto wt = new WorkerThread(0);
  if (!wt->start()) {
    delete wt;
    return false;
  }

  m_worker_thread = wt;
  return true;
}

void WorkerManager::stats(const std::function<void(stats::MetricDataSum&)> &cb) {
  if (m_worker_thread) {
    auto &main = Net::current();
    m_worker_thread->stats(
      [&, cb](stats::MetricData &metric_data) {
        main.post(
          [&, cb]() {
            m_metric_data_sum.sum(metric_data, true);
            cb(m_metric_data_sum);
          }
        );
      }
    );
  }
}

auto WorkerManager::stats() -> stats::MetricDataSum& {
  if (m_worker_thread) {
    std::mutex m;
    std::condition_variable cv;
    stats::MetricData *metric_data = nullptr;

    m_worker_thread->stats(
      [&](stats::MetricData &md) {
        {
          std::lock_guard<std::mutex> lock(m);
          metric_data = &md;
        }
        cv.notify_one();
      }
    );

    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&]{ return metric_data; });
    m_metric_data_sum.sum(*metric_data, true);
  }
  return m_metric_data_sum;
}

void WorkerManager::reload() {
  if (m_worker_thread) {
    m_worker_thread->reload();
  }
}

auto WorkerManager::stop(bool force) -> int {
  if (m_worker_thread) {
    return m_worker_thread->stop(force);
  } else {
    return 0;
  }
}

} // namespace pipy
