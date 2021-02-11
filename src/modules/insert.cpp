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

#include "insert.hpp"
#include "utils.hpp"

NS_BEGIN

Insert::Insert(bool replace) : m_replace(replace) {
}

Insert::~Insert() {
}

auto Insert::help() -> std::list<std::string> {
  if (m_replace) {
    return {
      "Replaces objects under a path",
      "path = Path under which objects are replaced",
      "from = Name of the queue where replacements are from",
      "default = Default object when nothing comes from the queue",
    };
  } else {
    return {
      "Adds objects under a path",
      "path = Path under which objects are added",
      "from = Name of the queue where added objects are from",
      "default = Default object when nothing comes from the queue",
    };
  }
}

void Insert::config(const std::map<std::string, std::string> &params) {
  m_match = Match(utils::get_param(params, "path"));
  m_from_name = utils::get_param(params, "from", "");
  auto def = utils::get_param(params, "default", "");

  if (m_from_name.empty() && def.empty()) {
    throw std::runtime_error("either 'from' or 'default' is required");
  }

  if (!def.empty()) {
    if (def[0] == '"') {
      if (def.back() == '"') def.pop_back();
      m_default.push_back(make_object<StringValue>(utils::unescape(def)));
    } else if (def == "{}") {
      m_default.push_back(make_object<MapStart>());
      m_default.push_back(make_object<MapEnd>());
    } else if (def == "[]") {
      m_default.push_back(make_object<ListStart>());
      m_default.push_back(make_object<ListEnd>());
    }
    else if (def == "null") m_default.push_back(make_object<NullValue>());
    else if (def == "true") m_default.push_back(make_object<BoolValue>(true));
    else if (def == "false") m_default.push_back(make_object<BoolValue>(false));
    else if (def.find_first_of(".eE") == std::string::npos) m_default.push_back(make_object<IntValue>(std::atoi(def.c_str())));
    else m_default.push_back(make_object<DoubleValue>(std::atof(def.c_str())));
  }
}

auto Insert::clone() -> Module* {
  auto clone = new Insert(m_replace);
  clone->m_match = m_match;
  clone->m_from_name = m_from_name;
  for (const auto &p : m_default) clone->m_default.push_back(clone_object(p));
  return clone;
}

void Insert::pipe(
  std::shared_ptr<Context> ctx,
  std::unique_ptr<Object> obj,
  Object::Receiver out
) {
  if (obj->is<SessionStart>() || obj->is<SessionEnd>() ||
      obj->is<MessageStart>() || obj->is<MessageEnd>())
  {
    m_match.reset();
    m_buffer.clear();
    m_from = nullptr;
    m_started = false;
    m_ended = false;
    m_inserted = false;
    out(std::move(obj));

  } else {
    m_match.process(obj.get());
    if (m_match.matching()) {
      if (!m_started) {
        m_started = true;
        if (m_match.is_map()) {
          out(make_object<MapKey>(m_match.key()));
        }
        if (m_from_name.empty()) {
          for (const auto &p : m_default) {
            out(clone_object(p));
          }
        } else {
          m_from = ctx->get_queue(m_from_name);
          m_from->receive([=](std::unique_ptr<Object> obj) {
            if (obj->is<MessageStart>()) return;
            if (m_from) {
              if (obj->is<MessageEnd>()) {
                if (!m_inserted && !m_default.empty()) {
                  for (const auto &p : m_default) {
                    out(clone_object(p));
                  }
                }
                for (auto &obj : m_buffer) out(std::move(obj));
                m_buffer.clear();
                m_from = nullptr;
              } else {
                out(std::move(obj));
                m_inserted = true;
              }
            }
          });
        }
      }
    } else if (m_started) {
      m_ended = true;
    }

    if (m_replace && m_started && !m_ended) {
      return; // discard
    }

    if (m_started && m_from) {
      m_buffer.push_back(std::move(obj));
    } else {
      out(std::move(obj));
    }
  }
}

NS_END