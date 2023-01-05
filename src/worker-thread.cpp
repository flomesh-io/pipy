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
#include "utils.hpp"

namespace pipy {

thread_local WorkerThread* WorkerThread::s_current = nullptr;

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
      s_current = this;

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
        init_metrics();
        m_recycle_timer = new Timer();
        clean_pools();
        Net::current().run();
        delete m_recycle_timer;
      }
    }
  );

  m_cv.wait(lock, [this]() { return m_started || m_failed; });
  return !m_failed;
}

void WorkerThread::status(const std::function<void(Status&)> &cb) {
  m_net->post(
    [=]() {
      Status::local.update();
      cb(Status::local);
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
        static std::string prefix("pjs::Constructor");
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

void WorkerThread::clean_pools() {
  for (const auto &p : pjs::Pool::all()) {
    p.second->clean();
  }
  m_recycle_timer->schedule(1, [this]() { clean_pools(); });
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

bool WorkerManager::start(int concurrency) {
  if (started()) return false;

  for (int i = 0; i < concurrency; i++) {
    auto wt = new WorkerThread(i);
    if (!wt->start()) {
      delete wt;
      stop(true);
      return false;
    }
    m_worker_threads.push_back(wt);
  }

  return true;
}

auto WorkerManager::status() -> Status& {
  if (auto n = m_worker_threads.size()) {
    std::mutex m;
    std::condition_variable cv;
    Status *statuses[n];

    for (auto *wt : m_worker_threads) {
      auto i = wt->index();
      wt->status(
        [&, i](Status &s) {
          {
            std::lock_guard<std::mutex> lock(m);
            statuses[i] = &s;
            n--;
          }
          cv.notify_one();
        }
      );
    }

    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&]{ return n == 0; });

    m_status = *statuses[0];
    m_status.timestamp = utils::now();
  }
  return m_status;
}

auto WorkerManager::stats() -> stats::MetricDataSum& {
  if (auto n = m_worker_threads.size()) {
    std::mutex m;
    std::condition_variable cv;
    stats::MetricData *metric_data[n];

    for (auto *wt : m_worker_threads) {
      auto i = wt->index();
      wt->stats(
        [&, i](stats::MetricData &md) {
          {
            std::lock_guard<std::mutex> lock(m);
            metric_data[i] = &md;
            n--;
          }
          cv.notify_one();
        }
      );
    }

    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&]{ return n == 0; });

    for (auto i = 0; i < m_worker_threads.size(); i++) {
      m_metric_data_sum.sum(*metric_data[i], i == 0);
    }
  }
  return m_metric_data_sum;
}

void WorkerManager::stats(const std::function<void(stats::MetricDataSum&)> &cb) {
  if (m_metric_data_sum_counter > 0) return;

  auto &main = Net::current();
  m_metric_data_sum_counter = m_worker_threads.size();

  for (auto *wt : m_worker_threads) {
    bool initial = (wt->index() == 0);
    wt->stats(
      [&, cb](stats::MetricData &metric_data) {
        main.post(
          [&, cb, initial]() {
            m_metric_data_sum.sum(metric_data, initial);
            m_metric_data_sum_counter--;
            if (!m_metric_data_sum_counter) cb(m_metric_data_sum);
          }
        );
      }
    );
  }
}

void WorkerManager::reload() {
  for (auto *wt : m_worker_threads) {
    wt->reload();
  }
}

auto WorkerManager::stop(bool force) -> int {
  int n = 0;
  for (auto *wt : m_worker_threads) {
    n += wt->stop(force);
  }
  return n;
}

} // namespace pipy
