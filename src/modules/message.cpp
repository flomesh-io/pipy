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

#include "message.hpp"
#include "utils.hpp"

NS_BEGIN

Message::Message() {
}

Message::~Message() {
}

auto Message::help() -> std::list<std::string> {
  return {
    "Outputs a structured message on reception of an input message",
    "content = Content of message as a stream of objects",
  };
}

void Message::config(const std::map<std::string, std::string> &params) {
  auto content = utils::get_param(params, "content");

  for (size_t i = 0; i < content.length(); i++) {
    auto ch = content[i];
    if (ch <= ' ') continue;

    std::string tok(1, ch);
    for (size_t j = i + 1; j <= content.length(); j++) {
      auto ch = content[j];
      if (!ch || (tok[0] == '"' && ch == '"') || (tok[0] != '"' && ch <= ' ')) {
        switch (tok[0]) {
        case '{': case '}':
        case '[': case ']':
          if (tok.length() > 1) throw std::runtime_error(std::string("invalid token: ") + tok);
          switch (tok[0]) {
            case '{': m_tokens.push_back({ Object::MapStart }); break;
            case '}': m_tokens.push_back({ Object::MapEnd }); break;
            case '[': m_tokens.push_back({ Object::ListStart }); break;
            case ']': m_tokens.push_back({ Object::ListEnd }); break;
          }
          break;
        case '.':
          m_tokens.push_back({ Object::MapKey, utils::unescape(&tok[1]) });
          break;
        case '"':
          m_tokens.push_back({ Object::StringValue, utils::unescape(&tok[1]) });
          break;
        default:
          if (tok == "null") {
            m_tokens.push_back({ Object::NullValue });
          } else if (tok == "true") {
            m_tokens.push_back({ Object::BoolValue, "T" });
          } else if (tok == "false") {
            m_tokens.push_back({ Object::BoolValue, "F" });
          } else if (tok[0] == '+' || tok[0] == '-' || std::isdigit(tok[0])) {
            if (tok.find_first_of(".eE") == std::string::npos) {
              m_tokens.push_back({ Object::IntValue, tok });
            } else {
              m_tokens.push_back({ Object::DoubleValue, tok });
            }
          } else {
            std::string msg("invalid token: ");
            throw std::runtime_error(msg + tok);
          }
          break;
        }
        i = j;
        break;

      } else {
        if (ch == '\\') {
          ch = content[++j];
          if (!ch) { --j; continue; }
          switch (ch) {
            case 'r': ch = '\r'; break;
            case 'n': ch = '\n'; break;
            case 't': ch = '\t'; break;
          }
        }
        if (ch) tok += ch;
      }
    }
  }
}

auto Message::clone() -> Module* {
  auto clone = new Message;
  clone->m_tokens = m_tokens;
  return clone;
}

void Message::pipe(
  std::shared_ptr<Context> ctx,
  std::unique_ptr<Object> obj,
  Object::Receiver out
) {
  switch (obj->type()) {
    case Object::SessionStart:
    case Object::SessionEnd:
      out(std::move(obj));
      break;
    case Object::MessageEnd: {
      out(make_object<MessageStart>());
      for (const auto &tok : m_tokens) {
        switch (tok.type) {
          case Object::NullValue:
            out(make_object<NullValue>());
            break;
          case Object::BoolValue:
            out(make_object<BoolValue>(tok.value[0] == 'T'));
            break;
          case Object::IntValue:
            out(make_object<IntValue>(std::atoi(tok.value.c_str())));
            break;
          case Object::LongValue:
            out(make_object<LongValue>(std::atoll(tok.value.c_str())));
            break;
          case Object::DoubleValue:
            out(make_object<DoubleValue>(std::atof(tok.value.c_str())));
            break;
          case Object::StringValue:
            out(make_object<StringValue>(ctx->evaluate(tok.value)));
            break;
          case Object::ListStart:
            out(make_object<ListStart>());
            break;
          case Object::ListEnd:
            out(make_object<ListEnd>());
            break;
          case Object::MapStart:
            out(make_object<MapStart>());
            break;
          case Object::MapKey:
            out(make_object<MapKey>(ctx->evaluate(tok.value)));
            break;
          case Object::MapEnd:
            out(make_object<MapEnd>());
            break;
          default:
            break;
        }
      }
      out(make_object<MessageEnd>());
      break;
    }
    default:
      break;
  }
}

NS_END
