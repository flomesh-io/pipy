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

#ifndef CODEBASE_HPP
#define CODEBASE_HPP

#include "pjs/pjs.hpp"
#include "fetch.hpp"

#include <mutex>
#include <functional>
#include <list>

namespace pipy {

class CodebaseStore;
class SharedData;

//
// Codebase
//

class Codebase {
public:

  //
  // Codebase::Watch
  //

  class Watch : public pjs::RefCount<Watch> {
  public:
    Watch(const std::function<void(const std::list<std::string> &)> &on_update)
      : m_on_update(on_update) {}

    bool closed() {
      m_mutex.lock();
      bool is_closed = (m_on_update == nullptr);
      m_mutex.unlock();
      return is_closed;
    }

    void close() {
      m_mutex.lock();
      m_on_update = nullptr;
      m_mutex.unlock();
    }

  private:
    void notify(const std::list<std::string> &filenames) {
      m_mutex.lock();
      if (m_on_update) m_on_update(filenames);
      m_mutex.unlock();
    }

    void cancel() {
      m_mutex.lock();
      if (m_on_update) m_on_update(std::list<std::string>());
      m_mutex.unlock();
    }

    std::function<void(const std::list<std::string> &)> m_on_update;
    std::mutex m_mutex;

    friend class Codebase;
  };

  static auto current() -> Codebase* { return s_current; }

  static Codebase* from_root(Codebase *root);
  static Codebase* from_fs(const std::string &path);
  static Codebase* from_fs(const std::string &path, const std::string &script);
  static Codebase* from_store(CodebaseStore *store, const std::string &name);
  static Codebase* from_http(const std::string &url, const Fetch::Options &options);

  void set_current() {
    if (s_current) s_current->deactivate();
    activate();
    s_current = this;
  }

  virtual ~Codebase() {}

  virtual auto version() const -> const std::string& = 0;
  virtual bool writable() const = 0;
  virtual auto entry() const -> const std::string& = 0;
  virtual void entry(const std::string &path) = 0;
  virtual void mount(const std::string &path, Codebase *codebase) = 0;
  virtual auto list(const std::string &path) -> std::list<std::string> = 0;
  virtual auto get(const std::string &path) -> SharedData* = 0;
  virtual void set(const std::string &path, SharedData *data) = 0;
  virtual void patch(const std::string &path, SharedData *data) = 0;
  virtual auto watch(const std::string &path, const std::function<void(const std::list<std::string> &)> &on_update) -> Watch* = 0;
  virtual void sync(bool force, const std::function<void(bool)> &on_update) = 0;

protected:
  virtual void activate() {};
  virtual void deactivate() {};

  void notify(Watch *w, const std::list<std::string> &filenames) { w->notify(filenames); }
  void cancel(Watch *w) { w->cancel(); }

  auto normalize_path(const std::string &path) -> std::string;

private:
  thread_local static Codebase* s_current;
};

} // namespace pipy

#endif // CODEBASE_HPP
