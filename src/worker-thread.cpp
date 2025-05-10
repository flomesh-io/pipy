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
#include "timer.hpp"
#include "api/console.hpp"
#include "api/pipy.hpp"
#include "net.hpp"
#include "log.hpp"
#include "utils.hpp"

namespace pipy {

thread_local WorkerThread* WorkerThread::s_current = nullptr;

WorkerThread::WorkerThread(WorkerManager *manager, int index)
  : m_manager(manager)
  , m_index(index)
  , m_working(false)
  , m_recycling(false)
  , m_shutdown(false)
  , m_done(false)
  , m_ended(false)
{
}

WorkerThread::~WorkerThread() {
  if (m_thread.joinable()) {
    m_thread.join();
  }
}

bool WorkerThread::start(bool force) {
  std::unique_lock<std::mutex> lock(m_start_cv_mutex);

  m_force_start = force;

  m_thread = std::thread(
    [this]() {
      s_current = this;
      main();
      m_ended.store(true);
      m_manager->on_thread_ended(m_index);
    }
  );

  m_start_cv.wait(lock, [this]() { return m_started || m_done || m_failed; });
  if (m_failed) throw std::runtime_error("failed to start worker thread");
  return m_started;
}

void WorkerThread::status(Status &status, const std::function<void()> &cb) {
  m_net->post(
    [&, cb]() {
      status.update_local();
      status.version = m_version;
      cb();
    }
  );
}

void WorkerThread::status(const std::function<void(Status&)> &cb) {
  m_net->post(
    [=]() {
      m_status.update_local();
      m_status.version = m_version;
      cb(m_status);
    }
  );
}

void WorkerThread::stats(stats::MetricData &metric_data, const std::vector<std::string> &names, const std::function<void()> &cb) {
  m_net->post(
    [&, cb]() {
      stats::MetricSet ms;
      for (const auto &name : names) {
        pjs::Ref<pjs::Str> s(pjs::Str::make(name));
        if (auto m = stats::Metric::local().get(s)) {
          ms.add(m);
        }
      }
      ms.collect();
      metric_data.update(ms);
      cb();
    }
  );
}

void WorkerThread::stats(stats::MetricData &metric_data, const std::function<void()> &cb) {
  m_net->post(
    [&, cb]() {
      stats::Metric::local().collect();
      metric_data.update(stats::Metric::local());
      cb();
    }
  );
}

void WorkerThread::stats(const std::function<void(stats::MetricData&)> &cb) {
  m_net->post(
    [=]() {
      stats::Metric::local().collect();
      m_metric_data.update(stats::Metric::local());
      cb(m_metric_data);
    }
  );
}

void WorkerThread::dump_objects(const std::string &class_name, std::map<std::string, size_t> &counts, const std::function<void()> &cb) {
  m_net->post(
    [&, cb]() {
      if (auto c = pjs::Class::get(class_name)) {
        c->iterate(
          [&](pjs::Object *obj) {
            auto &l = obj->location();
            if (auto m = l.module) {
              char str[100];
              auto len = std::snprintf(str, sizeof(str), ":%d:%d", l.line, l.column);
              auto key = m->name() + std::string(str, len);
              counts[key]++;
            }
            return true;
          }
        );
      }
      cb();
    }
  );
}

void WorkerThread::recycle() {
  if (m_working && !m_recycling) {
    m_recycling = true;
    m_net->post(
      [this]() {
        for (const auto &p : pjs::Pool::all()) {
          p.second->clean();
        }
        m_recycling = false;
      }
    );
  }
}

void WorkerThread::reload(const std::function<void(bool)> &cb) {
  m_net->post(
    [=]() {
      auto codebase = Codebase::current();

      auto &entry = codebase->entry();
      if (entry.empty()) {
        Log::error("[restart] Codebase has no entry point");
        cb(false);
        return;
      }

      Log::info("[restart] Reloading codebase on thread %d...", m_index);

      m_new_version = codebase->version();
      m_new_period = pjs::Promise::Period::make();
      m_new_worker = Worker::make(m_new_period);
      m_new_worker->set_forced();

      pjs::Ref<pjs::Promise::Period> old_period = pjs::Promise::Period::current();
      old_period->pause();
      m_new_period->pause();
      m_new_period->set_current();

      cb(m_new_worker->load_module(nullptr, entry));

      old_period->set_current();
      old_period->resume();
    }
  );
}

void WorkerThread::reload_done(bool ok) {
  if (ok) {
    m_net->post(
      [this]() {
        Listener::commit_all();
        if (m_new_worker) {
          pjs::Ref<Worker> current_worker = Worker::current();
          pjs::Ref<pjs::Promise::Period> current_period = pjs::Promise::Period::current();
          m_new_worker->start(true);
          current_worker->stop(true);
          current_period->end();
          m_new_period->set_current();
          m_new_period->resume();
          m_new_period = nullptr;
          m_new_worker = nullptr;
          m_version = m_new_version;
          m_working = true;
          if (m_workload_signal) m_workload_signal->fire();
          Log::info("[restart] Codebase reloaded on thread %d", m_index);
        }
      }
    );
  } else {
    m_net->post(
      [this]() {
        Listener::rollback_all();
        if (m_new_worker) {
          m_new_worker->stop(true);
          m_new_period->end();
          m_new_period = nullptr;
          m_new_worker = nullptr;
          m_new_version.clear();
          Log::error("[restart] Failed reloading codebase %d", m_index);
        }
      }
    );
  }
}

bool WorkerThread::stop(bool force) {
  if (force) {
    m_net->post(
      [this]() {
        m_shutdown = true;
        m_new_worker = nullptr;
        shutdown_all(true);
        Net::current().stop();
      }
    );
    m_thread.join();
    return true;

  } else {
    if (!m_shutdown) {
      m_shutdown = true;
      m_net->post(
        [this]() {
          m_new_worker = nullptr;
          shutdown_all(false);
        }
      );
      if (m_workload_signal) m_workload_signal->fire();
    }
    return m_ended.load();
  }
}

void WorkerThread::init_metrics() {
  pjs::Ref<pjs::Array> label_names = pjs::Array::make();

  //
  // Stats - size of allocated pool space
  //

  label_names->length(1);
  label_names->set(0, "class");

  stats::Gauge::make(
    pjs::Str::make("pipy_pool_allocated_size"),
    label_names,
    [](stats::Gauge *gauge) {
      double total = 0;
      for (const auto &i : pjs::Pool::all()) {
        auto c = i.second;
        auto n = c->allocated();
        if (n > 1) {
          pjs::Str *name = pjs::Str::make(c->name())->retain();
          auto metric = gauge->with_labels(&name, 1);
          auto size = n * c->size();
          metric->set(size);
          total += size;
          name->release();
        }
      }
      gauge->set(total);
    }
  );

  //
  // Stats - size of spare pool space
  //

  label_names->length(1);
  label_names->set(0, "class");

  stats::Gauge::make(
    pjs::Str::make("pipy_pool_spare_size"),
    label_names,
    [](stats::Gauge *gauge) {
      double total = 0;
      for (const auto &i : pjs::Pool::all()) {
        auto c = i.second;
        auto n = c->pooled();
        if (n > 0) {
          pjs::Str *name = pjs::Str::make(c->name())->retain();
          auto metric = gauge->with_labels(&name, 1);
          auto size = n * c->size();
          metric->set(size);
          total += size;
          name->release();
        }
      }
      gauge->set(total);
    }
  );

  //
  // Stats - # of objects
  //

  label_names->length(1);
  label_names->set(0, "class");

  stats::Gauge::make(
    pjs::Str::make("pipy_object_count"),
    label_names,
    [](stats::Gauge *gauge) {
      double total = 0;
      for (const auto &i : pjs::Class::all()) {
        static const std::string prefix("pjs::Constructor");
        if (utils::starts_with(i.second->name()->str(), prefix)) continue;
        if (auto n = i.second->object_count()) {
          pjs::Str *name = i.second->name();
          auto metric = gauge->with_labels(&name, 1);
          metric->set(n);
          total += n;
        }
      }
      gauge->set(total);
    }
  );

  //
  // Stats - # of chunks
  //

  label_names->length(1);
  label_names->set(0, "type");

  stats::Gauge::make(
    pjs::Str::make("pipy_chunk_count"),
    label_names,
    [](stats::Gauge *gauge) {
      if (WorkerThread::current()->index() > 0) return;
      double total = 0;
      Data::Producer::for_each([&](Data::Producer *producer) {
        if (auto n = producer->count()) {
          pjs::Ref<pjs::Str> str(pjs::Str::make(producer->name()));
          pjs::Str *name = str.get();
          auto metric = gauge->with_labels(&name, 1);
          metric->set(n);
          total += n;
        }
      });
      gauge->set(total);
    }
  );
}

void WorkerThread::shutdown_all(bool force) {
  if (auto period = pjs::Promise::Period::current()) period->cancel();
  if (auto worker = Worker::current()) worker->stop(force);
  Listener::for_each([&](Listener *l) { l->pipeline_layout(nullptr); return true; });
}

void WorkerThread::main() {
  Log::init();
  Pipy::argv(m_manager->m_argv);

  pjs::Promise::Period::set_uncaught_exception_handler(
    [](const pjs::Value &value) {
      Data buf;
      Console::dump(value, buf);
      Log::error("[pjs] Uncaught exception from promise: %s", buf.to_string().c_str());
    }
  );

  m_new_worker = Worker::make(
    pjs::Promise::Period::current(),
    m_manager->is_graph_enabled() && m_index == 0
  );

  if (m_force_start) {
    m_new_worker->set_forced();
  }

  auto &entry = Codebase::current()->entry();
  auto result = pjs::Value::empty;
  auto mod = m_new_worker->load_module(entry, result);
  bool failed = false;

  if (mod && m_new_worker->start(m_force_start)) {
    Listener::commit_all();
  } else {
    Listener::rollback_all();
    failed = true;
  }

  Net::context().poll();
  bool started = !Net::context().stopped();

  if (!started) {
    if (mod && mod->is_expression()) {
      if (result.is_string()) {
        std::cout << result.s()->str();
      } else {
        Data buf;
        Console::dump(result, buf);
        buf.to_chunks(
          [&](const uint8_t *ptr, int len) {
            std::cout.write((const char *)ptr, len);
          }
        );
        std::cout << std::endl;
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock(m_start_cv_mutex);
    m_started = started;
    m_failed = failed;
    m_done = !started && !failed;
    m_net = &Net::current();
    m_start_cv.notify_one();
  }

  if (started && !failed) {
    Log::debug(Log::THREAD, "[thread] Thread %d started", m_index);

    init_metrics();

    m_working = true;
    while (m_working) {
      Net::current().run();
      m_working = false;
      m_done = true;
      m_manager->on_thread_done(m_index);

      Log::debug(Log::THREAD, "[thread] Thread %d done", m_index);

      if (m_shutdown) break;

      m_workload_signal = std::unique_ptr<Signal>(
        new Signal([]() { Net::current().stop(); })
      );

      Net::current().restart();
      Net::current().run();

      m_workload_signal = nullptr;

      if (m_working) {
        m_done = false;
        Net::current().restart();
        Log::debug(Log::THREAD, "[thread] Thread %d restarted", m_index);
      }
    }

    Log::debug(Log::THREAD, "[thread] Thread %d ended", m_index);

  } else {
    m_new_worker->stop(true);
    m_new_worker = nullptr;
    m_manager->on_thread_done(m_index);
  }

  Log::shutdown();
  Listener::delete_all();
  Timer::cancel_all();
}

//
// WorkerManager
//

auto WorkerManager::get() -> WorkerManager& {
  static WorkerManager s_worker_manager;
  return s_worker_manager;
}

void WorkerManager::argv(const std::vector<std::string> &argv) {
  m_argv = argv;
}

bool WorkerManager::start(int concurrency, bool force) {
  if (started()) return false;

  m_concurrency = concurrency;
  m_stopping = false;
  m_stopped = false;

  for (int i = 0; i < concurrency; i++) {
    auto wt = new WorkerThread(this, i);
    auto rollback = [&]() {
      delete wt;
      stop(true);
    };
    try {
      if (!wt->start(force)) {
        rollback();
        return false;
      }
    } catch (std::runtime_error &) {
      rollback();
      throw;
    }
    m_worker_threads.push_back(wt);
  }

  return true;
}

auto WorkerManager::status() -> Status& {
  if (!m_querying_status && !m_reloading && !m_stopping) {
    m_querying_status = true;

    if (auto n = m_worker_threads.size()) {
      std::mutex m;
      std::condition_variable cv;
      pjs::vl_array<Status, 256> statuses(n);

      for (auto *wt : m_worker_threads) {
        auto i = wt->index();
        wt->status(
          statuses[i],
          [&]() {
            std::lock_guard<std::mutex> lock(m);
            n--;
            cv.notify_one();
          }
        );
      }

      std::unique_lock<std::mutex> lock(m);
      cv.wait(lock, [&]{ return n == 0; });

      m_status = std::move(statuses[0]);
      for (auto i = 1; i < m_worker_threads.size(); i++) {
        m_status.merge(statuses[i]);
      }
      m_status.update_global();
    }

    m_querying_status = false;
    check_reloading();
  }
  return m_status;
}

bool WorkerManager::status(const std::function<void(Status&)> &cb) {
  if (m_querying_status || m_reloading || m_stopping) return false;
  if (m_worker_threads.empty()) return false;

  m_querying_status = true;

  auto &main = Net::current();
  m_status_counter = 0;

  for (auto *wt : m_worker_threads) {
    wt->status(
      [&, cb](Status &s) {
        main.post(
          [&, cb]() {
            if (m_status_counter == 0) {
              m_status = std::move(s);
            } else {
              m_status.merge(s);
            }
            if (++m_status_counter == m_worker_threads.size()) {
              m_status.update_global();
              cb(m_status);
              m_querying_status = false;
              check_reloading();
            }
          }
        );
      }
    );
  }

  return true;
}

auto WorkerManager::stats() -> stats::MetricDataSum& {
  if (!m_querying_stats && !m_reloading && !m_stopping) {
    m_querying_stats = true;

    if (auto n = m_worker_threads.size()) {
      std::mutex m;
      std::condition_variable cv;
      pjs::vl_array<stats::MetricData, 256> metric_data(n);

      for (auto *wt : m_worker_threads) {
        auto i = wt->index();
        wt->stats(
          metric_data[i],
          [&]() {
            std::lock_guard<std::mutex> lock(m);
            n--;
            cv.notify_one();
          }
        );
      }

      std::unique_lock<std::mutex> lock(m);
      cv.wait(lock, [&]{ return n == 0; });

      for (auto i = 0; i < m_worker_threads.size(); i++) {
        m_metric_data_sum.sum(metric_data[i], i == 0);
      }
    }

    m_querying_stats = false;
    check_reloading();
  }

  return m_metric_data_sum;
}

bool WorkerManager::stats(const std::function<void(stats::MetricDataSum&)> &cb) {
  if (m_querying_stats || m_reloading || m_stopping) return false;
  if (m_worker_threads.empty()) return false;

  m_querying_stats = true;

  auto &main = Net::current();
  m_metric_data_sum_counter = 0;

  for (auto *wt : m_worker_threads) {
    wt->stats(
      [&, cb](stats::MetricData &metric_data) {
        main.post(
          [&, cb]() {
            m_metric_data_sum.sum(metric_data, m_metric_data_sum_counter == 0);
            if (++m_metric_data_sum_counter == m_worker_threads.size()) {
              cb(m_metric_data_sum);
              m_querying_stats = false;
              check_reloading();
            }
          }
        );
      }
    );
  }

  return true;
}

void WorkerManager::stats(const std::function<void(stats::MetricDataSum&)> &cb, const std::vector<std::string> &names) {
  auto req = new StatsRequest(this, names, cb);
  Net::main().post(
    [=]() {
      req->start();
    }
  );
}

auto WorkerManager::dump_objects(const std::string &class_name) -> std::map<std::string, size_t> {
  std::map<std::string, size_t> all;

  if (auto n = m_worker_threads.size()) {
    std::mutex m;
    std::condition_variable cv;
    std::vector<std::map<std::string, size_t>> counts(n);

    for (auto *wt : m_worker_threads) {
      auto i = wt->index();
      wt->dump_objects(
        class_name,
        counts[i],
        [&]() {
          std::lock_guard<std::mutex> lock(m);
          n--;
          cv.notify_one();
        }
      );
    }

    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&]{ return n == 0; });

    for (auto i = 0; i < m_worker_threads.size(); i++) {
      for (const auto &p : counts[i]) {
        all[p.first] += p.second;
      }
    }
  }

  return all;
}

void WorkerManager::recycle() {
  for (auto *wt : m_worker_threads) {
    wt->recycle();
  }
}

void WorkerManager::reload() {
  if (m_stopping) return;
  if (m_reloading || m_querying_status || m_querying_stats) {
    m_reloading_requested = true;
  } else {
    start_reloading();
  }
}

void WorkerManager::check_reloading() {
  if (m_reloading_requested) {
    m_reloading_requested = false;
    start_reloading();
  }
}

void WorkerManager::start_reloading() {
  if (auto n = m_worker_threads.size()) {
    m_reloading = true;

    std::mutex m;
    std::condition_variable cv;
    bool all_ok = true;

    for (auto *wt : m_worker_threads) {
      wt->reload(
        [&](bool ok) {
          std::lock_guard<std::mutex> lock(m);
          if (!ok) all_ok = false;
          n--;
          cv.notify_one();
        }
      );
    }

    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&]{ return n == 0; });

    for (auto *wt : m_worker_threads) {
      wt->reload_done(all_ok);
    }

    m_reloading = false;
  }
}

bool WorkerManager::stop(bool force) {
  if (m_stopped) return true;
  m_stopping = true;
  bool pending = false;
  for (auto *wt : m_worker_threads) {
    if (wt) {
      if (!wt->stop(force)) pending = true;
    }
  }
  if (pending) return false;
  for (auto *wt : m_worker_threads) {
    delete wt;
  }
  m_worker_threads.clear();
  m_status_counter = 0;
  m_metric_data_sum_counter = 0;
  m_stopped = true;
  return true;
}

void WorkerManager::on_thread_done(int index) {
  if (m_on_done) {
    Net::main().post(
      [this]() {
        for (auto *wt : m_worker_threads) {
          if (wt && !wt->done()) return;
        }
        m_on_done();
      }
    );
  }
}

void WorkerManager::on_thread_ended(int index) {
  if (m_on_ended) {
    Net::main().post(
      [this]() {
        for (auto *wt : m_worker_threads) {
          if (wt && !wt->ended()) return;
        }
        m_on_ended();
      }
    );
  }
}

//
// WorkerManager::StatsRequest
//

void WorkerManager::StatsRequest::start() {
  if (m_manager->m_querying_stats ||
      m_manager->m_reloading ||
      m_manager->m_worker_threads.empty()
  ) {
    m_from->post(
      [this]() {
        stats::MetricDataSum sum;
        m_cb(sum);
      }
    );
  } else if (auto thread_count = m_manager->m_worker_threads.size()) {
    m_threads.resize(thread_count);
    m_manager->m_querying_stats = true;
    for (auto *wt : m_manager->m_worker_threads) {
      wt->stats(
        m_threads[wt->index()],
        m_names,
        [=]() {
          m_from->post(
            [=]() {
              auto size = m_threads.size();
              if (++m_counter == size) {
                stats::MetricDataSum sum;
                for (size_t i = 0; i < size; i++) {
                  sum.sum(m_threads[i], i == 0);
                }
                m_cb(sum);
                m_manager->m_querying_stats = false;
                m_manager->check_reloading();
                delete this;
              }
            }
          );
        }
      );
    }
  } else {
    m_from->post(
      [=]() {
        stats::MetricDataSum sum;
        m_cb(sum);
        delete this;
      }
    );
  }
}

} // namespace pipy
