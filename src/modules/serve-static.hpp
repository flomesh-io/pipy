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

#ifndef SERVE_STATIC_HPP
#define SERVE_STATIC_HPP

#include "module.hpp"

#include <string>

NS_BEGIN

//
// ServeStatic
//

class ServeStatic : public Module {
public:
  ServeStatic();

  static void set_root_path(const std::string &path);

private:
  ~ServeStatic();

  virtual auto help() -> std::list<std::string> override;
  virtual void config(const std::map<std::string, std::string> &params) override;
  virtual auto clone() -> Module* override;
  virtual void pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) override;

  struct File {
    std::string content_type;
    std::string last_modified;
    Data data;
    Data data_gzip;
  };

  struct Cache {
    Cache(const std::string &path, const std::map<std::string, std::string> mime_types);
    std::map<std::string, File> files;
  };

  std::shared_ptr<Cache> m_cache;
  std::string m_var_method;
  std::string m_var_path;
  std::string m_var_status_code;
  std::string m_var_status;
  std::string m_var_accept_encoding;
  std::string m_var_content_encoding;
  std::string m_var_content_type;
  std::string m_var_last_modified;

  static std::string s_root_path;
};

NS_END

#endif // SERVE_STATIC_HPP
