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

WorkerThread::WorkerThread(Codebase *codebase, int index, const std::function<void()> &on_end)
  : m_index(index)
  , m_codebase(codebase)
  , m_version(codebase->version())
  , m_recycling(false)
  , m_ended(false)
  , m_on_end(on_end)
{
}

WorkerThread::~WorkerThread() {
  if (m_thread.joinable()) {
    m_thread.join();
  }
}

void WorkerThread::main(const std::vector<std::string> &argv) {
  m_net = &Net::current();

  Log::init();
  Pipy::argv(argv);

  pjs::Promise::Period::set_uncaught_exception_handler(
    [](const pjs::Value &value) {
      Data buf;
      Console::dump(value, buf);
      Log::error("[pjs] Uncaught exception from promise: %s", buf.to_string().c_str());
    }
  );

  m_codebase->set_current();
  m_worker = Worker::make(pjs::Promise::Period::current());

  auto result = pjs::Value::empty;
  auto mod = m_worker->load_module(m_codebase->entry(), result);

  if (mod && m_worker->start()) {
    Listener::commit_all();
    Net::context().poll();

    if (Net::context().stopped()) {
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

    } else if (mod->is_expression()) {
      init_metrics();
      Net::current().run();
    }

  } else {
    Listener::rollback_all();
  }

  Log::shutdown();
  Listener::delete_all();
  Timer::cancel_all();

  m_ended.store(true);

  if (m_on_end) m_on_end();
}

void WorkerThread::start(const std::vector<std::string> &argv) {
  m_thread = std::thread([&]() {
    main(argv);
  });
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

void WorkerThread::stats(stats::MetricData &metric_data, const std::function<void()> &cb) {
  m_net->post(
    [&, cb]() {
      stats::Metric::local().collect();
      metric_data.update(stats::Metric::local());
      cb();
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
  if (!m_recycling.load()) {
    m_recycling.store(true);
    m_net->post(
      [this]() {
        for (const auto &p : pjs::Pool::all()) {
          p.second->clean();
        }
        m_recycling.store(false);
      }
    );
  }
}

bool WorkerThread::signal(int sig) {
  switch (sig) {
    case SIGNAL_STOP:
      stop(m_has_shutdown);
      m_has_shutdown = true;
      return true;
  }
  return false;
}

void WorkerThread::stop(bool force) {
  if (force) {
    m_net->post(
      []() {
        shutdown_all(true);
        Net::current().stop();
      }
    );
    m_thread.join();
  } else {
    m_net->post(
      []() {
        shutdown_all(false);
      }
    );
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

//
// WorkerManager
//

thread_local WorkerManager* WorkerManager::s_current = nullptr;

WorkerManager::WorkerManager(Codebase *codebase, int concurrency)
  : m_codebase(codebase)
  , m_concurrency(concurrency)
{
}

void WorkerManager::start(const std::vector<std::string> &argv) {
  for (int i = 0; i < m_concurrency; i++) {
    auto wt = new WorkerThread(m_codebase, i, [=]() { on_thread_ended(i); });
    m_worker_threads.push_back(wt);
  }
  for (size_t i = 0; i < m_worker_threads.size(); i++) {
    m_worker_threads[i]->start(argv);
  }
}

void WorkerManager::status(const std::function<void(Status&)> &cb) {
  new StatusRequest(this, cb);
}

void WorkerManager::stats(const std::function<void(stats::MetricDataSum&)> &cb) {
  new StatsRequest(this, std::vector<std::string>(), cb);
}

void WorkerManager::stats(const std::vector<std::string> &names, const std::function<void(stats::MetricDataSum&)> &cb) {
  new StatsRequest(this, names, cb);
}

void WorkerManager::dump_objects(const std::string &class_name, const std::function<void(const std::map<std::string, size_t> &)> &cb) {
  new ObjectDumpRequest(this, class_name, cb);
}

void WorkerManager::stop(bool force) {
  for (auto *wt : m_worker_threads) {
    if (!wt->ended()) {
      wt->stop(force);
    }
  }
}

void WorkerManager::on_thread_ended(int index) {
  m_net->post([=]() {
    if (auto wt = m_worker_threads[index]) {
      for (auto *r : m_requests) {
        r->gather(wt, nullptr);
      }
    }
  });
}

//
// WorkerManager::Request
//

WorkerManager::Request::Request(WorkerManager *wm, const std::function<void()> &cb)
  : m_worker_manager(wm)
  , m_net(&Net::current())
  , m_callback(cb)
{
  wm->m_requests.insert(this);
}

WorkerManager::Request::~Request() {
  m_worker_manager->m_requests.erase(this);
}

void WorkerManager::Request::broadcast(const std::function<void(WorkerThread*)> &send_one) {
  for (auto *wt : m_worker_manager->m_worker_threads) {
    if (!wt->ended()) {
      m_threads.insert(wt);
      send_one(wt);
    }
  }
}

void WorkerManager::Request::gather(WorkerThread *wt, const std::function<void()> &aggregate) {
  m_net->post([=]() {
    if (aggregate) {
      aggregate();
    }
    m_threads.erase(wt);
    if (m_threads.empty()) {
      m_callback();
    }
  });
}

//
// WorkerManager::StatusRequest
//

WorkerManager::StatusRequest::StatusRequest(
  WorkerManager *wm,
  const std::function<void(Status &)> &cb
) : Request(wm, [=]() {
  m_status.update_global();
  cb(m_status);
  delete this;
}) {
  broadcast([&](WorkerThread *wt) {
    auto s = new Status;
    m_status_list.push_back(std::unique_ptr<Status>(s));
    wt->status(*s, [=]() {
      gather(wt, [=]() {
        if (m_initial) {
          m_status = std::move(*s);
          m_initial = false;
        } else {
          m_status.merge(*s);
        }
      });
    });
  });
}

//
// WorkerManager::StatsRequest
//

WorkerManager::StatsRequest::StatsRequest(
  WorkerManager *wm,
  const std::vector<std::string> &names,
  const std::function<void(stats::MetricDataSum &)> &cb
) : Request(wm, [=]() {
  cb(m_metric_data_sum);
  delete this;
}), m_names(names) {
  broadcast([&](WorkerThread *wt) {
    auto md = new stats::MetricData;
    m_metric_data.push_back(std::unique_ptr<stats::MetricData>(md));
    auto cb = [=]() {
      gather(wt, [=]() {
        m_metric_data_sum.sum(*md, m_initial);
        m_initial = false;
      });
    };
    if (m_names.empty()) {
      wt->stats(*md, cb);
    } else {
      wt->stats(*md, m_names, cb);
    }
  });
}

//
// WorkerManager::ObjectDumpRequest
//

WorkerManager::ObjectDumpRequest::ObjectDumpRequest(
  WorkerManager *wm,
  const std::string &class_name,
  const std::function<void(const std::map<std::string, size_t> &)> &cb
) : Request(wm, [=]() {
  cb(m_sum);
  delete this;
}), m_class_name(class_name) {
  broadcast([&](WorkerThread *wt) {
    auto c = new std::map<std::string, size_t>;
    m_counts.push_back(std::unique_ptr<std::map<std::string, size_t>>(c));
    wt->dump_objects(m_class_name, *c, [=]() {
      gather(wt, [=]() {
        for (const auto &p : *c) {
          m_sum[p.first] += p.second;
        }
      });
    });
  });
}

} // namespace pipy
