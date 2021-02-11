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

#include "counter.hpp"
#include "metrics.hpp"
#include "utils.hpp"
#include "logging.hpp"

#include <iostream>
#include <ctime>

NS_BEGIN

Counter::Counter() {
}

Counter::~Counter() {
}

auto Counter::help() -> std::list<std::string> {
  return {
    "Tracks number of objects or data bytes in the stream",
    "when = Type of object to count, options including SessionStart, SessionEnd, MessageStart, MessageEnd and Data",
    "label = Name of the count",
    "latency_since = When measuring latencies, the variable name of a previously recorded timestamp to measure from",
    "latency_buckets = When measuring latencies, the bucket limits from low to high, separated by commas",
  };
}

void Counter::config(const std::map<std::string, std::string> &params) {
  auto when = utils::get_param(params, "when");
  if (when == "SessionStart") m_when = Object::SessionStart; else
  if (when == "SessionEnd") m_when = Object::SessionEnd; else
  if (when == "MessageStart") m_when = Object::MessageStart; else
  if (when == "MessageEnd") m_when = Object::MessageEnd; else
  if (when == "Data") m_when = Object::Data; else {
    std::string msg("invalid value for parameter when: ");
    throw std::runtime_error(msg + when);
  }

  m_label = utils::get_param(params, "label");
  m_latency_since = utils::get_param(params, "latency_since", "");
  m_latency_buckets = std::make_shared<std::vector<int>>();

  auto buckets = utils::get_param(params, "latency_buckets", "");
  for (size_t i = 0; i < buckets.size(); i++) {
    size_t j = i;
    while (j < buckets.size() && buckets[j] != ',') j++;
    std::string item(buckets.c_str() + i, j - i);
    m_latency_buckets->push_back(std::atoi(item.c_str()));
    i = j;
  }

  if (!m_latency_since.empty() && m_latency_buckets->empty()) {
    throw std::runtime_error("parameter latency_buckets is required");
  }

  if (m_latency_since.empty() && !m_latency_buckets->empty()) {
    throw std::runtime_error("parameter latency_since is required");
  }
}

auto Counter::clone() -> Module* {
  auto clone = new Counter();
  clone->m_when = m_when;
  clone->m_label = m_label;
  clone->m_latency_since = m_latency_since;
  clone->m_latency_buckets = m_latency_buckets;
  return clone;
}

void Counter::pipe(
  std::shared_ptr<Context> ctx,
  std::unique_ptr<Object> obj,
  Object::Receiver out
) {
  if (obj->type() == m_when) {
    auto label = ctx->evaluate(m_label);
    auto delta = m_when == Object::Data ? obj->as<Data>()->size() : 1;

    if (m_latency_since.empty()) {
      Metrics::increase(label, delta);

    } else {
      std::string since;
      if (ctx->find(m_latency_since, since)) {
        auto s = std::atoll(since.c_str());
        auto t = std::chrono::system_clock::now().time_since_epoch();
        auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(t).count() - s;
        for (auto bucket : *m_latency_buckets) {
          if (latency <= bucket) {
            Metrics::increase(label, delta, bucket);
            break;
          }
        }
      } else {
        Log::warn("context variable not found: %s", m_latency_since.c_str());
      }
    }
  }

  out(std::move(obj));
}

NS_END
