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

#include "url.hpp"
#include "utils.hpp"

#include <sstream>

namespace pipy {

//
// URL
//

URL::URL(pjs::Str *url) : URL(url->str())
{
}

URL::URL(pjs::Str *url, pjs::Str *base) : URL(url->str(), base->str())
{
}

URL::URL(const std::string &url, const std::string &base) {
  auto find_protocol = [](const std::string &url) -> std::string {
    for (int i = 0; i < url.length(); i++) {
      auto c = url[i];
      if (c == ':') {
        if (i > 0) return url.substr(0, i + 1);
        break;
      }
      if (i == 0) {
        if (!std::isalpha(c)) break;
      } else {
        if (!std::isalnum(c) && c != '+' && c != '-' && c != '.') break;
      }
    }
    return std::string();
  };

  auto find_host = [](const std::string &path) -> std::string {
    if (path.length() >= 2 && path[0] == '/' && path[1] == '/') {
      auto i = path.find_first_not_of('/');
      if (i != std::string::npos) {
        i = path.find('/', i);
        return i == std::string::npos ? path : path.substr(0, i);
      }
    }
    return std::string();
  };

  auto trim_host = [](const std::string &host) -> std::string {
    auto i = host.find_first_not_of('/');
    return i == std::string::npos ? host : host.substr(i);
  };

  auto protocol = find_protocol(url);
  auto host_path = url.substr(protocol.length());
  auto host = find_host(host_path);
  auto path = host_path.substr(host.length());
  host = trim_host(host);

  if (host.empty() && !base.empty()) {
    protocol = find_protocol(base);
    host_path = base.substr(protocol.length());
    host = find_host(host_path);
    if (path.empty()) {
      path = host_path.substr(host.length());
    } else if (path[0] != '/') {
      auto base = host_path.substr(host.length());
      auto i = base.rfind('/');
      if (i != std::string::npos) base = base.substr(0, i);
      path = base + '/' + path;
    }
    host = trim_host(host);
  }

  std::string auth, username, password;
  auto i = host.find('@');
  if (i != std::string::npos) {
    auth = host.substr(0,i);
    host = host.substr(i+1);
    auto i = auth.find(':');
    if (i == std::string::npos) {
      username = auth;
    } else {
      username = auth.substr(0,i);
      password = auth.substr(i+1);
    }
  }

  std::string hostname, port;
  if (host[0] == '[') {
    i = host.find(']');
    if (i != std::string::npos && host[i+1] == ':') {
      hostname = host.substr(0, i + 1);
      port = host.substr(i + 2);
    } else {
      hostname = host;
    }
  } else {
    i = host.find(':');
    if (i != std::string::npos) {
      hostname = host.substr(0, i);
      port = host.substr(i + 1);
    } else {
      hostname = host;
    }
  }

  std::string hash;
  i = path.rfind('#');
  if (i != std::string::npos) {
    hash = path.substr(i);
    path = path.substr(0, i);
  }

  std::string pathname, search, query;
  i = path.find('?');
  if (i != std::string::npos) {
    pathname = path.substr(0, i);
    search = path.substr(i);
    query = path.substr(i + 1);
  } else {
    pathname = path;
  }

  // TODO: Make pathname in canonical form

  if (protocol.empty()) protocol = "http:";
  if (path.empty()) { path = pathname = "/"; }
  if (port.empty()) {
    if (protocol == "ftp:") port = "21";
    else if (protocol == "gopher:") port = "70";
    else if (protocol == "http:") port = "80";
    else if (protocol == "https:") port = "443";
    else if (protocol == "ws:") port = "80";
    else if (protocol == "wss:") port = "443";
  }

  auto origin = protocol + "//";
  auto href = origin;
  if (!auth.empty()) href += (auth + '@');

  origin += host;
  href += host + path + hash;

  m_auth     = pjs::Str::make(auth     );
  m_hash     = pjs::Str::make(hash     );
  m_host     = pjs::Str::make(host     );
  m_hostname = pjs::Str::make(hostname );
  m_href     = pjs::Str::make(href     );
  m_origin   = pjs::Str::make(origin   );
  m_password = pjs::Str::make(password );
  m_path     = pjs::Str::make(path     );
  m_pathname = pjs::Str::make(pathname );
  m_port     = pjs::Str::make(port     );
  m_protocol = pjs::Str::make(protocol );
  m_query    = pjs::Str::make(query    );
  m_search   = pjs::Str::make(search   );
  m_username = pjs::Str::make(username );

  m_search_params = URLSearchParams::make(m_search);
}

//
// URLSearchParams
//

URLSearchParams::URLSearchParams(pjs::Str *search) {
  m_params = pjs::Object::make();
  auto str = search->str();
  int i = 0;
  if (str[0] == '?') i++;
  while (i < str.length()) {
    int j = i;
    while (j < str.length() && str[j] != '&') j++;
    if (j > i) {
      int k = i;
      while (k < j && str[k] != '=') k++;
      if (k < j) {
        append(
          utils::decode_uri(str.substr(i, k - i)),
          utils::decode_uri(str.substr(k + 1, j - k - 1))
        );
      } else {
        append(
          utils::decode_uri(str.substr(i, j - i)),
          std::string()
        );
      }
    }
    i = j + 1;
  }
}

URLSearchParams::URLSearchParams(pjs::Object *search) {
  m_params = pjs::Object::make();
  search->iterate_all(
    [this](pjs::Str *k, pjs::Value &v) {
      set(k, v);
    }
  );
}

auto URLSearchParams::getAll(pjs::Str *name) -> pjs::Array* {
  pjs::Value v;
  m_params->get(name, v);
  if (v.is_string()) {
    auto *arr = pjs::Array::make(1);
    arr->set(0, v);
    return arr;
  } else if (v.is_array()) {
    return v.as<pjs::Array>()->map(
      [](pjs::Value &val, int, pjs::Value &ret) {
        ret = val;
        return true;
      }
    );
  } else {
    return pjs::Array::make();
  }
}

auto URLSearchParams::get(pjs::Str *name) -> pjs::Str* {
  pjs::Value v;
  m_params->get(name, v);
  if (v.is_string()) {
    return v.s();
  } else if (v.is_array()) {
    v.as<pjs::Array>()->get(0, v);
    if (v.is_string()) {
      return v.s();
    }
  }
  return nullptr;
}

void URLSearchParams::set(pjs::Str *name, const pjs::Value &value) {
  if (value.is_undefined() || value.is_null()) {
    m_params->ht_delete(name);
  } else if (value.is_array()) {
    m_params->set(name, value.as<pjs::Array>()->map(
      [](pjs::Value &val, int, pjs::Value &ret) {
        auto s = val.to_string();
        ret.set(s);
        s->release();
        return true;
      }
    ));
  } else {
    auto s = value.to_string();
    m_params->set(name, s);
    s->release();
  }
}

auto URLSearchParams::toObject() -> pjs::Object* {
  auto obj = pjs::Object::make();
  m_params->iterate_all(
    [&](pjs::Str *k, pjs::Value &v) {
      if (v.is_string()) {
        obj->set(k, v);
      } else if (v.is_array()) {
        auto a = v.as<pjs::Array>();
        obj->set(k, a->map(
          [](pjs::Value &v, int, pjs::Value &v2) {
            v2 = v;
            return true;
          }
        ));
      }
    }
  );
  return obj;
}

auto URLSearchParams::to_string() const -> std::string {
  std::stringstream ss;
  bool first = true;
  m_params->iterate_all(
    [&](pjs::Str *k, pjs::Value &v) {
      if (v.is_string()) {
        if (first) first = false; else ss << '&';
        ss << utils::encode_uri(k->str());
        ss << '=';
        ss << utils::encode_uri(v.s()->str());
      } else if (v.is_array()) {
        v.as<pjs::Array>()->iterate_all(
          [&](pjs::Value &v, int) {
            if (first) first = false; else ss << '&';
            ss << utils::encode_uri(k->str());
            ss << '=';
            ss << utils::encode_uri(v.s()->str());
          }
        );
      }
    }
  );
  return ss.str();
}

void URLSearchParams::append(const std::string &name, const std::string &value) {
  pjs::Ref<pjs::Str> k(pjs::Str::make(name));
  pjs::Ref<pjs::Str> v(pjs::Str::make(value));
  pjs::Value old;
  m_params->get(k, old);
  if (old.is_array()) {
    old.as<pjs::Array>()->push(v.get());
  } else if (old.is_string()) {
    auto arr = pjs::Array::make(2);
    arr->set(0, old);
    arr->set(1, v.get());
    m_params->set(k, arr);
  } else {
    m_params->set(k, v.get());
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

//
// URL
//

template<> void ClassDef<URL>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *url, *base = nullptr;
    if (!ctx.arguments(1, &url, &base)) return nullptr;
    return URL::make(url, base);
  });

  accessor("auth",          [](Object *obj, Value &ret) { ret.set(obj->as<URL>()->auth()); });
  accessor("hash",          [](Object *obj, Value &ret) { ret.set(obj->as<URL>()->hash()); });
  accessor("host",          [](Object *obj, Value &ret) { ret.set(obj->as<URL>()->host()); });
  accessor("hostname",      [](Object *obj, Value &ret) { ret.set(obj->as<URL>()->hostname()); });
  accessor("href",          [](Object *obj, Value &ret) { ret.set(obj->as<URL>()->href()); });
  accessor("origin",        [](Object *obj, Value &ret) { ret.set(obj->as<URL>()->origin()); });
  accessor("password",      [](Object *obj, Value &ret) { ret.set(obj->as<URL>()->password()); });
  accessor("path",          [](Object *obj, Value &ret) { ret.set(obj->as<URL>()->path()); });
  accessor("pathname",      [](Object *obj, Value &ret) { ret.set(obj->as<URL>()->pathname()); });
  accessor("port",          [](Object *obj, Value &ret) { ret.set(obj->as<URL>()->port()); });
  accessor("protocol",      [](Object *obj, Value &ret) { ret.set(obj->as<URL>()->protocol()); });
  accessor("query",         [](Object *obj, Value &ret) { ret.set(obj->as<URL>()->query()); });
  accessor("search",        [](Object *obj, Value &ret) { ret.set(obj->as<URL>()->search()); });
  accessor("searchParams",  [](Object *obj, Value &ret) { ret.set(obj->as<URL>()->searchParams()); });
  accessor("username",      [](Object *obj, Value &ret) { ret.set(obj->as<URL>()->username()); });
}

template<> void ClassDef<Constructor<URL>>::init() {
  super<Function>();
  ctor();
}

//
// URLSearchParams
//

template<> void ClassDef<URLSearchParams>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *search;
    Object *params;
    if (ctx.try_arguments(1, &search)) {
      return URLSearchParams::make(search);
    } else if (ctx.try_arguments(1, &params)) {
      return URLSearchParams::make(params);
    } else {
      ctx.error_argument_type(0, "a string or an object");
      return nullptr;
    }
  });

  method("getAll", [](Context &ctx, Object *obj, Value &ret) {
    Str *name;
    if (!ctx.arguments(1, &name)) return;
    ret.set(obj->as<URLSearchParams>()->getAll(name));
  });

  method("get", [](Context &ctx, Object *obj, Value &ret) {
    Str *name;
    if (!ctx.arguments(1, &name)) return;
    if (auto *s = obj->as<URLSearchParams>()->get(name)) {
      ret.set(s);
    } else {
      ret = Value::null;
    }
  });

  method("set", [](Context &ctx, Object *obj, Value &ret) {
    Str *name;
    Value value;
    if (!ctx.arguments(1, &name, &value)) return;
    obj->as<URLSearchParams>()->set(name, value);
  });

  method("toObject", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<URLSearchParams>()->toObject());
  });
}

template<> void ClassDef<Constructor<URLSearchParams>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs
