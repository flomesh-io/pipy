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

#include "thread.hpp"
#include "worker-thread.hpp"

namespace pipy {

auto Thread::current() -> Thread* {
  thread_local static pjs::Ref<Thread> s_thread;
  if (!s_thread) s_thread = Thread::make();
  return s_thread;
}

auto Thread::index() const -> int {
  if (auto wt = WorkerThread::current()) {
    return wt->index();
  } else {
    return -1;
  }
}

auto Thread::concurrency() const -> int {
  auto &wm = WorkerManager::get();
  return wm.concurrency();
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Thread>::init() {
  accessor("id", [](Object *obj, Value &ret) { ret.set(obj->as<Thread>()->index()); });
  accessor("concurrency", [](Object *obj, Value &ret) { ret.set(obj->as<Thread>()->concurrency()); });
}

} // namespace pjs
