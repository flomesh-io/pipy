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

#ifndef UPDATER_HPP
#define UPDATER_HPP

#include "pjs/pjs.hpp"
#include "timer.hpp"
#include "fetch.hpp"

#include <functional>
#include <list>
#include <map>

namespace pipy {

class CodebaseReceiver;
class Data;
class Pipeline;
class Session;
class URL;

//
// Codebase
//

class Codebase {
public:
  static auto current() -> Codebase* { return s_current; }

  void set_current() {
    s_current = this;
  }

  virtual ~Codebase() {}

  virtual auto entry() const -> const std::string& = 0;
  virtual auto list(const std::string &path) -> std::list<std::string> = 0;
  virtual auto get(const std::string &path) -> Data* = 0;
  virtual void set(const std::string &path, Data *data) = 0;
  virtual void check(const std::function<void(bool)> &on_complete) = 0;
  virtual void update(const std::function<void(bool)> &on_complete) = 0;

private:
  static Codebase* s_current;
};

//
// CodebaseFS
//

class CodebaseFS : public Codebase {
public:
  CodebaseFS(const std::string &path);

  auto base() const -> const std::string& { return m_base; }

  virtual auto entry() const -> const std::string& override { return m_entry; }
  virtual auto list(const std::string &path) -> std::list<std::string> override;
  virtual auto get(const std::string &path) -> Data* override;
  virtual void set(const std::string &path, Data *data) override;
  virtual void check(const std::function<void(bool)> &on_complete) override;
  virtual void update(const std::function<void(bool)> &on_complete) override;

private:
  std::string m_base;
  std::string m_entry;
  Timer m_timer;
};

//
// CodebaseHTTP
//

class CodebaseHTTP : public Codebase {
public:
  CodebaseHTTP(const std::string &url);
  ~CodebaseHTTP();

  virtual auto entry() const -> const std::string& override { return m_entry; }

  virtual auto list(const std::string &path) -> std::list<std::string> override;

  virtual auto get(const std::string &path) -> pipy::Data* override {
    auto i = m_files.find(path);
    if (i == m_files.end()) return nullptr;
    return i->second;
  }

  virtual void set(const std::string &path, Data *data) override {}

  virtual void check(const std::function<void(bool)> &on_complete) override;
  virtual void update(const std::function<void(bool)> &on_complete) override;

private:
  pjs::Ref<URL> m_url;
  Fetch m_fetch;
  std::string m_etag;
  std::string m_date;
  std::string m_base;
  std::string m_root;
  std::string m_entry;
  std::map<std::string, pjs::Ref<pipy::Data>> m_files;
  std::map<std::string, pjs::Ref<pipy::Data>> m_dl_temp;
  std::list<std::string> m_dl_list;

  void download_next(const std::function<void(bool)> &on_complete);
};

} // namespace pipy

#endif // UPDATER_HPP