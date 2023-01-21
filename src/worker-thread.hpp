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

#ifndef WORKER_THREAD_HPP
#define WORKER_THREAD_HPP

#include "net.hpp"
#include "status.hpp"
#include "api/stats.hpp"

#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <functional>
#include <vector>

namespace pipy {

class Worker;

//
// WorkerThread
//

class WorkerThread {
public:
  WorkerThread(int index, bool is_graph_enabled);
  ~WorkerThread();

  static auto current() -> WorkerThread* { return s_current; }

  auto index() const -> int { return m_index; }
  auto active_pipeline_count() const -> size_t { return m_active_pipeline_count.load(std::memory_order_relaxed); }

  bool start();
  void status(Status &status, const std::function<void()> &cb);
  void status(const std::function<void(Status&)> &cb);
  void stats(stats::MetricData &metric_data, const std::function<void()> &cb);
  void stats(const std::function<void(stats::MetricData&)> &cb);
  void reload(const std::function<void(bool)> &cb);
  void reload_done(bool ok);
  bool stop(bool force = false);

private:
  int m_index;
  Net* m_net = nullptr;
  std::string m_version;
  std::string m_new_version;
  pjs::Ref<Worker> m_new_worker;
  Status m_status;
  stats::MetricData m_metric_data;
  std::atomic<size_t> m_active_pipeline_count;
  std::atomic<bool> m_shutdown;
  std::atomic<bool> m_done;
  std::thread m_thread;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  bool m_graph_enabled = false;
  bool m_started = false;
  bool m_failed = false;

  static void init_metrics();
  static void shutdown_all();

  void main();
  void recycle();

  thread_local static WorkerThread* s_current;
};

//
// WorkerManager
//

class WorkerManager {
public:
  static auto get() -> WorkerManager&;

  void enable_graph(bool b) { m_graph_enabled = b; }
  bool started() const { return !m_worker_threads.empty(); }
  bool start(int concurrency = 1);
  void status(Status &status);
  void status(const std::function<void(Status&)> &cb);
  void stats(int i, stats::MetricData &stats);
  void stats(stats::MetricDataSum &stats);
  void stats(const std::function<void(stats::MetricDataSum&)> &cb);
  void reload();
  auto active_pipeline_count() -> size_t;
  bool stop(bool force = false);

private:
  std::vector<WorkerThread*> m_worker_threads;
  Status m_status;
  int m_status_counter = -1;
  stats::MetricDataSum m_metric_data_sum;
  int m_metric_data_sum_counter = -1;
  bool m_graph_enabled = false;
};

} // namespace pipy

#endif // WORKER_THREAD_HPP
