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

#ifndef URL_HPP
#define URL_HPP

#include "pjs/pjs.hpp"

namespace pipy {

class URLSearchParams;

//
// URL
//

class URL : public pjs::ObjectTemplate<URL> {
public:
  auto auth() const -> pjs::Str* { return m_auth; }
  auto hash() const -> pjs::Str* { return m_hash; }
  auto host() const -> pjs::Str* { return m_host; }
  auto hostname() const -> pjs::Str* { return m_hostname; }
  auto href() const -> pjs::Str* { return m_href; }
  auto origin() const -> pjs::Str* { return m_origin; }
  auto password() const -> pjs::Str* { return m_password; }
  auto path() const -> pjs::Str* { return m_path; }
  auto pathname() const -> pjs::Str* { return m_pathname; }
  auto port() const -> pjs::Str* { return m_port; }
  auto protocol() const -> pjs::Str* { return m_protocol; }
  auto query() const -> pjs::Str* { return m_query; }
  auto search() const -> pjs::Str* { return m_search; }
  auto searchParams() const -> URLSearchParams* { return m_search_params; }
  auto username() const -> pjs::Str* { return m_username; }

private:
  URL(pjs::Str *url);
  URL(pjs::Str *url, pjs::Str *base);

  pjs::Ref<pjs::Str> m_auth;
  pjs::Ref<pjs::Str> m_hash;
  pjs::Ref<pjs::Str> m_host;
  pjs::Ref<pjs::Str> m_hostname;
  pjs::Ref<pjs::Str> m_href;
  pjs::Ref<pjs::Str> m_origin;
  pjs::Ref<pjs::Str> m_password;
  pjs::Ref<pjs::Str> m_path;
  pjs::Ref<pjs::Str> m_pathname;
  pjs::Ref<pjs::Str> m_port;
  pjs::Ref<pjs::Str> m_protocol;
  pjs::Ref<pjs::Str> m_query;
  pjs::Ref<pjs::Str> m_search;
  pjs::Ref<URLSearchParams> m_search_params;
  pjs::Ref<pjs::Str> m_username;

  friend class pjs::ObjectTemplate<URL>;
};

//
// URLSearchParams
//

class URLSearchParams : public pjs::ObjectTemplate<URLSearchParams> {
public:
  auto getAll(pjs::Str *name) -> pjs::Array*;
  auto get(pjs::Str *name) -> pjs::Str*;
  void set(pjs::Str *name, const pjs::Value &value);

private:
  URLSearchParams(pjs::Str *search);
  URLSearchParams(pjs::Object *search);

  virtual auto to_string() const -> std::string override;

  void append(const std::string &name, const std::string &value);

  pjs::Ref<pjs::Object> m_params;

  friend class pjs::ObjectTemplate<URLSearchParams>;
};

} // namespace pipy

#endif // URL_HPP
