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
#include "list.hpp"
#include "status.hpp"
#include "api/stats.hpp"
#include "signal.hpp"

#include <thread>
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <functional>
#include <vector>

namespace pipy {

class Worker;
class WorkerManager;
class PipelineLoadBalancer;

//
// WorkerThread
//

class WorkerThread {
public:
  WorkerThread(WorkerManager *manager, int index);
  ~WorkerThread();

  static auto current() -> WorkerThread* { return s_current; }

  auto manager() const -> WorkerManager* { return m_manager; }
  auto index() const -> int { return m_index; }
  bool done() const { return m_done; }
  bool ended() const { return m_ended; }

  bool start(bool force);
  void status(Status &status, const std::function<void()> &cb);
  void status(const std::function<void(Status&)> &cb);
  void stats(stats::MetricData &metric_data, const std::vector<std::string> &names, const std::function<void()> &cb);
  void stats(stats::MetricData &metric_data, const std::function<void()> &cb);
  void stats(const std::function<void(stats::MetricData&)> &cb);
  void stats(const std::vector<std::string> &names, const std::function<void(stats::MetricData&)> &cb);
  void dump_objects(const std::string &class_name, std::map<std::string, size_t> &counts, const std::function<void()> &cb);
  void recycle();
  void reload(const std::function<void(bool)> &cb);
  void reload_done(bool ok);
  void exit(const std::function<void()> &cb);
  bool stop(bool force = false);

private:
  WorkerManager* m_manager;
  int m_index;
  Net* m_net = nullptr;
  std::string m_version;
  std::string m_new_version;
  pjs::Ref<Worker> m_new_worker;
  Status m_status;
  stats::MetricData m_metric_data;
  std::atomic<bool> m_working;
  std::atomic<bool> m_recycling;
  std::atomic<bool> m_shutdown;
  std::atomic<bool> m_done;
  std::atomic<bool> m_ended;
  std::thread m_thread;
  std::condition_variable m_start_cv;
  std::mutex m_start_cv_mutex;
  std::unique_ptr<Signal> m_workload_signal;
  pjs::Ref<pjs::Promise::Period> m_new_period;
  bool m_force_start = false;
  bool m_started = false;
  bool m_failed = false;

  static void init_metrics();
  static void shutdown_all(bool force);

  void main();

  thread_local static WorkerThread* s_current;
};

//
// WorkerManager
//

class WorkerManager {
public:
  static auto get() -> WorkerManager&;

  bool is_graph_enabled() const { return m_graph_enabled; }
  void enable_graph(bool b) { m_graph_enabled = b; }
  void on_done(const std::function<void()> &cb) { m_on_done = cb; }
  void on_ended(const std::function<void()> &cb) { m_on_ended = cb; }
  void argv(const std::vector<std::string> &argv);
  bool started() const { return !m_worker_threads.empty(); }
  bool start(int concurrency = 1, bool force = false);
  auto status() -> Status&;
  bool status(const std::function<void(Status&)> &cb);
  auto stats() -> stats::MetricDataSum&;
  bool stats(const std::function<void(stats::MetricDataSum&)> &cb);
  void stats(const std::function<void(stats::MetricDataSum&)> &cb, const std::vector<std::string> &names);
  auto dump_objects(const std::string &class_name) -> std::map<std::string, size_t>;
  void recycle();
  void reload();
  bool admin(pjs::Str *path, const Data &request, const std::function<void(const Data *)> &respond);
  auto concurrency() const -> int { return m_concurrency; }
  bool stop(bool force = false);

private:
  class StatsRequest {
  public:
    StatsRequest(
      WorkerManager *manager,
      const std::vector<std::string> &names,
      const std::function<void(stats::MetricDataSum&)> &cb
    ) : m_manager(manager)
      , m_from(&Net::current())
      , m_names(names)
      , m_cb(cb) {}

    void start();

  private:
    WorkerManager* m_manager;
    Net* m_from;
    std::vector<std::string> m_names;
    std::vector<stats::MetricData> m_threads;
    int m_counter = 0;
    std::function<void(stats::MetricDataSum&)> m_cb;
  };

  std::vector<WorkerThread*> m_worker_threads;
  std::vector<std::string> m_argv;
  Status m_status;
  int m_status_counter = -1;
  stats::MetricDataSum m_metric_data_sum;
  int m_metric_data_sum_counter = -1;
  int m_concurrency = 0;
  bool m_graph_enabled = false;
  bool m_reloading_requested = false;
  bool m_reloading = false;
  bool m_querying_status = false;
  bool m_querying_stats = false;
  bool m_stopping = false;
  bool m_stopped = false;
  std::function<void()> m_on_done;
  std::function<void()> m_on_ended;

  void check_reloading();
  void start_reloading();
  void next_admin_request();
  void on_thread_done(int index);
  void on_thread_ended(int index);

  friend class WorkerThread;
};

} // namespace pipy

#endif // WORKER_THREAD_HPP
