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
#include "pipeline-lb.hpp"
#include "timer.hpp"
#include "api/configuration.hpp"
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
  , m_active_pipeline_count(0)
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
      m_new_worker = Worker::make(m_manager->loading_pipeline_lb());
      m_new_worker->set_forced();

      pjs::Ref<pjs::Promise::Period> old_period = pjs::Promise::Period::current();
      old_period->pause();

      m_new_period = pjs::Promise::Period::make();
      m_new_period->pause();
      m_new_period->set_current();

      cb(
        m_new_worker->load_js_module(entry) &&
        m_new_worker->bind()
      );

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

void WorkerThread::admin(pjs::Str *path, SharedData *request, const std::function<void(SharedData*)> &respond) {
  auto name = path->data()->retain();
  request->retain();
  m_net->post(
    [=]() {
      if (auto worker = Worker::current()) {
        Data buf;
        request->to_data(buf);
        auto head = http::RequestHead::make();
        head->path = pjs::Str::make(name);
        pjs::Ref<Message> req = Message::make(head, Data::make(std::move(buf)));
        if (!worker->admin(
          req.get(),
          [=](Message *response) {
            auto body = response->body();
            auto data = body ? SharedData::make(*body) : SharedData::make(Data());
            data->retain();
            Net::main().post([=]() { respond(data); data->release(); });
          }
        )) {
          Net::main().post([=]() { respond(nullptr); });
        }
      } else {
        Net::main().post([=]() { respond(nullptr); });
      }
      request->release();
      name->release();
    }
  );
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
      double total = 0;
      Data::Producer::for_each([&](Data::Producer *producer) {
        if (auto n = producer->count()) {
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

void WorkerThread::shutdown_all(bool force) {
  if (auto period = pjs::Promise::Period::current()) period->end();
  if (auto worker = Worker::current()) worker->stop(force);
  Listener::for_each([&](Listener *l) { l->pipeline_layout(nullptr); return true; });
}

void WorkerThread::main() {
  Log::init();
  Pipy::argv(m_manager->m_argv);

  m_new_worker = Worker::make(
    m_manager->loading_pipeline_lb(),
    m_manager->is_graph_enabled() && m_index == 0
  );

  if (m_force_start) {
    m_new_worker->set_forced();
  }

  auto &entry = Codebase::current()->entry();
  auto result = pjs::Value::empty;
  auto mod = m_new_worker->load_js_module(entry, result);
  bool failed = false;

  if (mod && m_new_worker->bind() && m_new_worker->start(m_force_start)) {
    Listener::commit_all();
  } else {
    Listener::rollback_all();
    failed = true;
  }

  Net::context().poll();
  bool started = !Net::context().stopped();

  if (!started) {
    if (mod && mod->is_expression() && !result.is<Configuration>()) {
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
    Log::info("[worker] Thread %d started", m_index);

    init_metrics();

    m_working = true;
    while (m_working) {
      Net::current().run();
      m_working = false;
      m_done = true;
      m_manager->on_thread_done(m_index);

      Log::info("[worker] Thread %d done", m_index);

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
        Log::info("[worker] Thread %d restarted", m_index);
      }
    }

    Log::info("[worker] Thread %d ended", m_index);

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
  m_loading_pipeline_lb = PipelineLoadBalancer::make();

  for (int i = 0; i < concurrency; i++) {
    auto wt = new WorkerThread(this, i);
    auto rollback = [&]() {
      delete wt;
      stop(true);
      m_loading_pipeline_lb = nullptr;
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

  m_running_pipeline_lb = m_loading_pipeline_lb;
  m_loading_pipeline_lb = nullptr;

  return true;
}

auto WorkerManager::status() -> Status& {
  if (!m_querying_status && !m_reloading) {
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
  if (m_querying_status || m_reloading) return false;
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
  if (!m_querying_stats && !m_reloading) {
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
  if (m_querying_stats || m_reloading) return false;
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

void WorkerManager::recycle() {
  for (auto *wt : m_worker_threads) {
    wt->recycle();
  }
}

void WorkerManager::reload() {
  if (m_reloading || m_querying_status || m_querying_stats || !m_admin_requests.empty()) {
    m_reloading_requested = true;
  } else {
    start_reloading();
  }
}

bool WorkerManager::admin(pjs::Str *path, const Data &request, const std::function<void(const Data *)> &respond) {
  if (m_reloading) return false;
  if (m_worker_threads.empty()) return false;
  new AdminRequest(this, path, request, respond);
  next_admin_request();
  return true;
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
    m_loading_pipeline_lb = PipelineLoadBalancer::make();

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

    if (all_ok) {
      m_running_pipeline_lb = m_loading_pipeline_lb;
    }

    m_loading_pipeline_lb = nullptr;
    m_reloading = false;
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

void WorkerManager::next_admin_request() {
  if (!m_current_admin_request) {
    if (auto r = m_admin_requests.head()) {
      m_current_admin_request = r;
      r->start();
    } else {
      check_reloading();
    }
  }
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

//
// WorkerManager::AdminRequest
//

WorkerManager::AdminRequest::AdminRequest(WorkerManager *manager, pjs::Str *path, const Data &request, const std::function<void(const Data *)> &respond)
  : m_manager(manager)
  , m_path(path)
  , m_request(request)
  , m_responses(manager->m_worker_threads.size())
  , m_respond(respond)
{
  manager->m_admin_requests.push(this);
}

WorkerManager::AdminRequest::~AdminRequest() {
  m_manager->m_admin_requests.remove(this);
  m_manager->next_admin_request();
}

void WorkerManager::AdminRequest::start() {
  pjs::Ref<SharedData> req = SharedData::make(m_request);
  for (size_t i = 0; i < m_manager->m_worker_threads.size(); i++) {
    m_manager->m_worker_threads[i]->admin(
      m_path, req, [=](SharedData *res) {
        auto &r = m_responses[i];
        if (res) {
          res->to_data(r.data);
          r.successful = true;
        } else {
          r.successful = false;
        }
        if (++m_response_count == m_responses.size()) {
          Data response;
          bool successful = false;
          for (const auto &r : m_responses) {
            if (r.successful) {
              successful = true;
              response.push(r.data);
            }
          }
          m_respond(successful ? &response : nullptr);
          m_manager->m_current_admin_request = nullptr;
          delete this;
        }
      }
    );
  }
}

} // namespace pipy
