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
#include "codebase.hpp"
#include "status.hpp"
#include "api/stats.hpp"
#include "signal.hpp"

#include <thread>
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <functional>
#include <vector>

namespace pipy {

class Worker;
class WorkerManager;

//
// WorkerThread
//

class WorkerThread {
public:
  WorkerThread(Codebase *codebase, int index = 0, const std::function<void()> &on_end = nullptr);
  ~WorkerThread();

  static auto current() -> WorkerThread* { return s_current; }

  auto index() const -> int { return m_index; }
  bool ended() const { return m_ended.load(); }

  void main(const std::vector<std::string> &argv);
  void start(const std::vector<std::string> &argv);
  void status(Status &status, const std::function<void()> &cb);
  void stats(stats::MetricData &metric_data, const std::function<void()> &cb);
  void stats(stats::MetricData &metric_data, const std::vector<std::string> &names, const std::function<void()> &cb);
  void dump_objects(const std::string &class_name, std::map<std::string, size_t> &counts, const std::function<void()> &cb);
  void recycle();
  bool signal(int sig);
  void stop(bool force = false);

private:
  int m_index;
  Net* m_net = nullptr;
  pjs::Ref<Worker> m_worker;
  Codebase* m_codebase;
  std::string m_version;
  std::atomic<bool> m_recycling;
  std::atomic<bool> m_ended;
  std::thread m_thread;
  std::function<void()> m_on_end;
  bool m_has_shutdown = false;

  static void init_metrics();
  static void shutdown_all(bool force);

  thread_local static WorkerThread* s_current;
};

//
// WorkerManager
//

class WorkerManager {
public:
  static auto current() -> WorkerManager* { return s_current; }

  static auto make(Codebase *codebase, int concurrency = 1) -> WorkerManager* {
    return new WorkerManager(codebase, concurrency);
  }

  void set_current() { s_current = this; }
  void start(const std::vector<std::string> &argv);
  void status(const std::function<void(Status&)> &cb);
  void stats(const std::function<void(stats::MetricDataSum&)> &cb);
  void stats(const std::vector<std::string> &names, const std::function<void(stats::MetricDataSum&)> &cb);
  void dump_objects(const std::string &class_name, const std::function<void(const std::map<std::string, size_t> &)> &cb);
  auto concurrency() const -> int { return m_concurrency; }
  void stop(bool force = false);

private:
  WorkerManager(Codebase *codebase, int concurrency);

  //
  // WorkerManager::Request
  //

  class Request {
  protected:
    Request(WorkerManager *wm, const std::function<void()> &cb);
    ~Request();
    void broadcast(const std::function<void(WorkerThread*)> &send_one);
  public:
    void gather(WorkerThread *wt, const std::function<void()> &aggregate);
  private:
    WorkerManager* m_worker_manager;
    Net* m_net;
    std::function<void()> m_callback;
    std::set<WorkerThread*> m_threads;
  };

  //
  // WorkerManager::StatusRequest
  //

  class StatusRequest : public Request {
  public:
    StatusRequest(
      WorkerManager *wm,
      const std::function<void(Status &)> &cb
    );
  private:
    std::vector<std::unique_ptr<Status>> m_status_list;
    Status m_status;
    bool m_initial = true;
  };

  //
  // WorkerManager::StatsRequest
  //

  class StatsRequest : public Request {
  public:
    StatsRequest(
      WorkerManager *wm,
      const std::vector<std::string> &names,
      const std::function<void(stats::MetricDataSum &)> &cb
    );
  private:
    std::vector<std::string> m_names;
    std::vector<std::unique_ptr<stats::MetricData>> m_metric_data;
    stats::MetricDataSum m_metric_data_sum;
    bool m_initial = true;
  };

  //
  // WorkerManager::ObjectDumpRequest
  //

  class ObjectDumpRequest : public Request {
  public:
    ObjectDumpRequest(
      WorkerManager *wm,
      const std::string &class_name,
      const std::function<void(const std::map<std::string, size_t> &)> &cb
    );
  private:
    std::string m_class_name;
    std::vector<std::unique_ptr<std::map<std::string, size_t>>> m_counts;
    std::map<std::string, size_t> m_sum;
  };

  Net* m_net;
  Codebase* m_codebase;
  std::vector<WorkerThread*> m_worker_threads;
  std::set<Request*> m_requests;
  int m_concurrency = 0;

  void on_thread_ended(int index);

  thread_local static WorkerManager* s_current;
};

} // namespace pipy

#endif // WORKER_THREAD_HPP
