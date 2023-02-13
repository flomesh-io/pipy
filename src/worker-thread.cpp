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
#include "net.hpp"
#include "log.hpp"
#include "api/logging.hpp"
#include "utils.hpp"

namespace pipy {

thread_local WorkerThread* WorkerThread::s_current = nullptr;

WorkerThread::WorkerThread(WorkerManager *manager, int index, bool is_graph_enabled)
  : m_manager(manager)
  , m_index(index)
  , m_active_pipeline_count(0)
  , m_working(false)
  , m_recycling(false)
  , m_shutdown(false)
  , m_ended(false)
  , m_graph_enabled(is_graph_enabled)
{
}

WorkerThread::~WorkerThread() {
  if (m_thread.joinable()) {
    m_thread.join();
  }
}

bool WorkerThread::start() {
  std::unique_lock<std::mutex> lock(m_mutex);

  m_thread = std::thread(
    [this]() {
      s_current = this;
      main();
      m_ended.store(true, std::memory_order_relaxed);
    }
  );

  m_cv.wait(lock, [this]() { return m_started || m_failed; });
  return !m_failed;
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

void WorkerThread::stats(stats::MetricData &metric_data, const std::function<void()> &cb) {
  m_net->post(
    [&, cb]() {
      stats::Metric::local().collect_all();
      metric_data.update(stats::Metric::local());
      cb();
    }
  );
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

void WorkerThread::recycle() {
  if (m_working && !m_recycling) {
    m_recycling = true;
    m_net->post(
      [this]() {
        for (const auto &p : pjs::Pool::all()) {
          p.second->clean();
        }
        auto n = PipelineLayout::active_pipeline_count();
        if (!n && m_shutdown) Net::current().stop();
        m_active_pipeline_count = n;
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
      m_new_worker = Worker::make();

      cb(m_new_worker->load_js_module(entry) && m_new_worker->bind());
    }
  );
}

void WorkerThread::reload_done(bool ok) {
  if (ok) {
    m_net->post(
      [this]() {
        if (m_new_worker) {
          pjs::Ref<Worker> current_worker = Worker::current();
          m_new_worker->start(true);
          current_worker->stop();
          m_new_worker = nullptr;
          m_version = m_new_version;
          m_working = true;
          Log::info("[restart] Codebase reloaded on thread %d", m_index);
        }
      }
    );
  } else {
    m_net->post(
      [this]() {
        if (m_new_worker) {
          m_new_worker->stop();
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
        m_new_worker = nullptr;
        shutdown_all();
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
          shutdown_all();
        }
      );
    }
    return m_ended.load(std::memory_order_relaxed);
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
      double total = 0;
      Data::Producer::for_each([&](Data::Producer *producer) {
        if (auto n = producer->current()) {
          pjs::Str *name = producer->name();
          auto metric = gauge->with_labels(&name, 1);
          metric->set(n);
          total += n;
        }
      });
      gauge->set(total);
    }
  );

  //
  // Stats - # of pipelines
  //

  label_names->length(2);
  label_names->set(0, "module");
  label_names->set(1, "name");

  stats::Gauge::make(
    pjs::Str::make("pipy_pipeline_count"),
    label_names,
    [](stats::Gauge *gauge) {
      double total = 0;
      PipelineLayout::for_each(
        [&](PipelineLayout *p) {
          if (auto mod = dynamic_cast<JSModule*>(p->module())) {
            if (auto n = p->active()) {
              pjs::Str *labels[2];
              labels[0] = mod ? mod->filename() : pjs::Str::empty.get();
              labels[1] = p->name_or_label();
              auto metric = gauge->with_labels(labels, 2);
              metric->set(n);
              total += n;
            }
          }
        }
      );
      gauge->set(total);
    }
  );
}

void WorkerThread::shutdown_all() {
  if (auto worker = Worker::current()) worker->stop();
  logging::Logger::shutdown_all();
  Listener::for_each([&](Listener *l) { l->pipeline_layout(nullptr); });
}

void WorkerThread::main() {
  Log::init();

  auto &entry = Codebase::current()->entry();
  auto worker = Worker::make(m_graph_enabled);
  auto mod = worker->load_js_module(entry);
  bool started = (mod && worker->bind() && worker->start(false));

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_started = started;
    m_failed = !started;
    m_net = &Net::current();
  }

  m_cv.notify_one();

  if (started) {
    Log::info("[start] Thread %d started", m_index);

    init_metrics();

    m_working = true;
    while (m_working) {
      Net::current().run();
      m_working = false;
      m_manager->on_thread_done(m_index);

      Log::info("[start] Thread %d done", m_index);

      Timer timer;
      std::function<void()> wait_for_work;
      wait_for_work = [&]() {
        if (!m_working && !m_shutdown) {
          timer.schedule(1, wait_for_work);
        }
      };

      wait_for_work();
      Net::current().context().restart();
      Net::current().run();

      if (m_working) {
        Net::current().context().restart();
        Log::info("[start] Thread %d restarted", m_index);
      }
    }

    Log::info("[start] Thread %d ended", m_index);

  } else {
    Log::error("[start] Thread %d failed to start", m_index);
  }

  Log::shutdown();
  Timer::cancel_all();
}

//
// WorkerManager
//

auto WorkerManager::get() -> WorkerManager& {
  static WorkerManager s_worker_manager;
  return s_worker_manager;
}

bool WorkerManager::start(int concurrency) {
  if (started()) return false;

  for (int i = 0; i < concurrency; i++) {
    auto wt = new WorkerThread(this, i, m_graph_enabled && (i == 0));
    if (!wt->start()) {
      delete wt;
      stop(true);
      return false;
    }
    m_worker_threads.push_back(wt);
  }

  return true;
}

void WorkerManager::status(Status &status) {
  if (auto n = m_worker_threads.size()) {
    std::mutex m;
    std::condition_variable cv;
    Status statuses[n];

    for (auto *wt : m_worker_threads) {
      auto i = wt->index();
      wt->status(
        statuses[i],
        [&]() {
          {
            std::lock_guard<std::mutex> lock(m);
            n--;
          }
          cv.notify_one();
        }
      );
    }

    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&]{ return n == 0; });

    status = std::move(statuses[0]);
    for (auto i = 1; i < m_worker_threads.size(); i++) {
      status.merge(statuses[i]);
    }
    status.update_global();
  }
}

void WorkerManager::status(const std::function<void(Status&)> &cb) {
  if (m_worker_threads.empty()) return;
  if (m_status_counter >= 0) return;

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
              m_status_counter = -1;
            }
          }
        );
      }
    );
  }
}

void WorkerManager::stats(int i, stats::MetricData &stats) {
  if (0 <= i && i < m_worker_threads.size()) {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;

    if (auto *wt = m_worker_threads[i]) {
      wt->stats(
        stats,
        [&]() {
          {
            std::lock_guard<std::mutex> lock(m);
            done = true;
          }
          cv.notify_one();
        }
      );
    }

    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&]{ return done; });
  }
}

void WorkerManager::stats(stats::MetricDataSum &stats) {
  if (auto n = m_worker_threads.size()) {
    std::mutex m;
    std::condition_variable cv;
    stats::MetricData metric_data[n];

    for (auto *wt : m_worker_threads) {
      auto i = wt->index();
      wt->stats(
        metric_data[i],
        [&]() {
          {
            std::lock_guard<std::mutex> lock(m);
            n--;
          }
          cv.notify_one();
        }
      );
    }

    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&]{ return n == 0; });

    for (auto i = 0; i < m_worker_threads.size(); i++) {
      stats.sum(metric_data[i], i == 0);
    }
  }
}

void WorkerManager::stats(const std::function<void(stats::MetricDataSum&)> &cb) {
  if (m_worker_threads.empty()) return;
  if (m_metric_data_sum_counter >= 0) return;

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
              m_metric_data_sum_counter = -1;
            }
          }
        );
      }
    );
  }
}

void WorkerManager::recycle() {
  for (auto *wt : m_worker_threads) {
    wt->recycle();
  }
}

void WorkerManager::reload() {
  if (auto n = m_worker_threads.size()) {
    std::mutex m;
    std::condition_variable cv;
    bool all_ok = true;

    for (auto *wt : m_worker_threads) {
      wt->reload(
        [&](bool ok) {
          {
            std::lock_guard<std::mutex> lock(m);
            if (!ok) all_ok = false;
            n--;
          }
          cv.notify_one();
        }
      );
    }

    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&]{ return n == 0; });

    for (auto *wt : m_worker_threads) {
      wt->reload_done(all_ok);
    }
  }
}

auto WorkerManager::active_pipeline_count() -> size_t {
  size_t n = 0;
  for (auto *wt : m_worker_threads) {
    if (wt) {
      n += wt->active_pipeline_count();
    }
  }
  return n;
}

bool WorkerManager::stop(bool force) {
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
  return true;
}

void WorkerManager::on_thread_done(int index) {
  if (m_on_done) {
    Net::main().post(
      [this]() {
        for (auto *wt : m_worker_threads) {
          if (wt && wt->working()) return;
        }
        m_on_done();
      }
    );
  }
}

} // namespace pipy
