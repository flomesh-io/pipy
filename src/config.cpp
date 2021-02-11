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

#include "config.hpp"
#include "utils.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <vector>

NS_BEGIN

static Config::Pipeline* s_current_pipeline = nullptr;

static auto expand_env_vars(const std::string &str) -> std::string {
  std::string result;
  for (size_t i = 0; i < str.length(); ++i) {
    auto ch = str[i];
    if (ch == '$' && str[i+1] == '(') {
      size_t s = i + 2, j = s;
      while (j < str.length() && str[j] != ')') ++j;
      std::string name(str.c_str() + s, j - s);
      if (auto val = getenv(name.c_str())) {
        result += val;
      }
      i = j;
    } else {
      result += ch;
    }
  }
  return result;
}

bool Config::parse_file(const std::string &pathname) {
  std::cout << "Loading configuration file " << pathname << "...\n";
  std::ifstream fs(pathname, std::ios::in);
  if (!fs.is_open()) {
    std::cerr << "Failed to open configuration file " << pathname << std::endl;
    return false;
  }
  std::stringstream ss;
  fs >> ss.rdbuf();
  if (!parse_str(ss.str().c_str())) return false;
  draw();
  std::cout << std::endl;
  std::cout << pipelines.size() << " pipeline(s) in total" << std::endl;
  return true;
}

void Config::dump() {
  for (const auto &pipeline : pipelines) {
    std::cout << "pipeline " << pipeline.name << std::endl;
    for (const auto &module : pipeline.modules) {
      std::cout << "  " << module.name << std::endl;
      for (const auto &kv : module.params) {
        std::cout << "    " << kv.first << " = " << kv.second << std::endl;
      }
    }
  }
}

void Config::draw() {
  static std::string box_tl(",-"), box_tr("-,");
  static std::string box_ml("| "), box_mr(" |");
  static std::string box_bl("'-"), box_br("-'");

  static std::string line_in(">>--");
  static std::string line_tl(",----"), line_tr("----,");
  static std::string line_ml("|    "), line_mr("    |");
  static std::string line_bl("'-->>"), line_br("<<--'");

  auto pad = [](const std::string &str, size_t width) {
    if (str.length() >= width) return str;
    return str + std::string(width - str.length(), ' ');
  };

  auto space = [](size_t width) {
    return std::string(width, ' ');
  };

  auto line = [](size_t width) {
    return std::string(width, '-');
  };

  auto extend = [](const std::string &str, size_t delta) {
    auto p = str.find('-');
    if (p == std::string::npos) return str + std::string(delta, '-');
    return str.substr(0, p) + std::string(delta, '-') + str.substr(p);
  };

  struct SizeInfo {
    size_t w, k, v;
  };

  const size_t MAX_VALUE_WIDTH = 30;

  for (const auto &pipeline : pipelines) {
    std::vector<SizeInfo> sizes;
    for (const auto &module : pipeline.modules) {
      SizeInfo size = { 0 };
      for (const auto &kv : module.params) {
        const auto &k = kv.first;
        if (k.length() > size.k) size.k = k.length();
        const auto vs = utils::split(kv.second, '\n');
        for (const auto &s : vs) size.v = std::max(size.v, s.length());
      }
      size.v = std::min(size.v, MAX_VALUE_WIDTH);
      size.w = std::max(module.name.length(), 2 + size.k + 3 + size.v);
      size.v = std::max(size.v, size.w - 2 - size.k - 3);
      sizes.push_back(size);
    }

    std::cout << std::endl;
    std::cout << pipeline.name << std::endl;
    std::cout << line_in;

    if (sizes.size() == 0) {
      auto w = pipeline.name.length();
      std::cout << extend(line_tr, w) << std::endl;
      std::cout << space(w + line_in.length()) << line_mr << std::endl;
      std::cout << extend(line_br, w + line_in.length()) << std::endl;
      continue;
    }

    std::cout << line(line_bl.length());
    std::cout << line(box_tl.length() + sizes[0].w + box_tr.length());
    std::cout << line_tr << std::endl;

    size_t i = 0;
    bool left = false;

    for (const auto &module : pipeline.modules) {
      const auto &size = sizes[i];
      size_t padding = 0;
      if (left && i + 1 < sizes.size() && size.w < sizes[i+1].w) {
        padding = sizes[i+1].w - size.w;
      } else if (!left && i > 0 && size.w < sizes[i-1].w) {
        padding = sizes[i-1].w - size.w;
      }

      if (left) {
        std::cout << space(line_in.length()) << line_ml;
        std::cout << box_tl << line(size.w) << box_tr;
        std::cout << std::endl;
      } else {
        std::cout << space(line_in.length()) << space(line_ml.length());
        std::cout << box_tl << line(size.w) << box_tr;
        std::cout << space(padding) << line_mr << std::endl;
      }

      std::vector<std::string> lines;
      lines.push_back(pad(module.name, size.w));
      for (const auto &kv : module.params) {
        const auto &k = kv.first;
        const auto vs = utils::split(kv.second, '\n');
        auto head = true;
        for (const auto &s : vs) {
          for (size_t i = 0; i < s.length(); i += MAX_VALUE_WIDTH) {
            auto v = s.substr(i, std::min(s.length() - i, MAX_VALUE_WIDTH));
            if (head) {
              lines.push_back(space(2) + pad(k, size.k) + " = " + pad(v, size.v));
              head = false;
            } else {
              lines.push_back(space(2 + size.k + 3) + pad(v, size.v));
            }
          }
        }
      }

      for (size_t i = 0, n = lines.size() - 1; i <= n; i++) {
        std::cout << space(line_in.length());
        if (left) {
          std::cout << (i == 0 ? line_bl : space(line_bl.length()));
        } else {
          std::cout << (i == n ? line_tl : space(line_tl.length()));
        }
        std::cout << box_ml;
        std::cout << lines[i];
        std::cout << box_mr;
        if (left) {
          std::cout << (i == n ? extend(line_tr, padding) : space(padding + line_tr.length()));
        } else {
          std::cout << (i == 0 ? extend(line_br, padding) : space(padding + line_br.length()));
        }
        std::cout << std::endl;
      }

      if (left) {
        std::cout << space(line_in.length()) << space(line_ml.length());
        std::cout << box_bl << line(size.w) << box_br;
        std::cout << space(padding) << line_mr << std::endl;
      } else {
        std::cout << space(line_in.length()) << line_ml;
        std::cout << box_bl << line(size.w) << box_br;
        std::cout << std::endl;
      }

      left = !left;
      i++;
    }

    if (left) {
      std::cout << line_br;
      std::cout << std::endl;
    } else {
      auto w = line_in.length() + line_bl.length() + box_bl.length() + sizes.back().w + box_br.length();
      std::cout << extend(line_br, w);
      std::cout << std::endl;
    }
  }
}

bool Config::parse_str(const char *str) {
  m_parser_stack.clear();
  int line_num = 1;
  size_t i = 0;
  for (;;) {
    size_t j = i;
    while (str[j] && str[j] != '\n') j++;
    if (!parse_line(std::string(&str[i], j - i), line_num++)) return false;
    if (!str[j]) break;
    i = j + 1;
  }
  return true;
}

bool Config::parse_line(const std::string &str, int num) {
  size_t indent = 0;
  while (indent < str.length() && str[indent] <= ' ') indent++;
  if (indent == str.length()) return true;

  auto line = utils::trim(str);
  char quoted = '\0';
  for (size_t i = 0; i < line.length(); i++) {
    auto c = line[i];
    if (quoted) {
      if (c == '\\') i++;
      else if (c == quoted) quoted = '\0';
    } else {
      if (c == '"' || c == '\'') quoted = c;
      else if (c == '#') {
        line = utils::trim(line.substr(0, i));
        break;
      }
    }
  }

  if (line.empty()) return true;

  while (m_parser_stack.size() > 0 && indent <= m_parser_stack.back().indent) {
    m_parser_stack.pop_back();
  }

  Context ctx;
  ctx.indent = indent;

  if (!m_parser_stack.empty()) {
    const auto &top = m_parser_stack.back();
    ctx.level = top.level + 1;
    ctx.pipeline = top.pipeline;
    ctx.module = top.module;
    ctx.header = top.header;
  }

  auto i = line.find_first_of(ctx.level >= 2 ? "=" : "\t ");
  auto header = utils::trim(i == std::string::npos ? line : line.substr(0, i));
  auto value = utils::trim(i == std::string::npos ? std::string() : line.substr(i + 1));

  switch (ctx.level) {
    case 0: {
      if (header != "pipy") {
        parse_error("expected to begin with 'pipy'", num);
        return false;
      }
      m_parser_stack.push_back(ctx);
      break;
    }
    case 1: {
      if (header != "pipeline") {
        parse_error("expected to begin with 'pipeline'", num);
        return false;
      }
      if (value.empty()) {
        parse_error("pipeline address expected", num);
        return false;
      }
      auto &pipeline = *pipelines.emplace(pipelines.end());
      pipeline.name = value;
      pipeline.line = num;
      ctx.pipeline = &pipeline;
      m_parser_stack.push_back(ctx);
      break;
    }
    case 2: {
      if (!value.empty()) {
        parse_error("expected only a module name", num);
        return false;
      }
      auto p = ctx.pipeline;
      auto &module = *p->modules.emplace(p->modules.end());
      module.name = header;
      module.line = num;
      ctx.module = &module;
      m_parser_stack.push_back(ctx);
      break;
    }
    case 3: {
      if (header.empty()) {
        parse_error("expected a parameter name", num);
        return false;
      }
      if (value.empty()) {
        parse_error("expected a value", num);
        return false;
      }
      ctx.module->params[header] = expand_env_vars(value);
      ctx.header = header;
      m_parser_stack.push_back(ctx);
      break;
    }
    case 4: {
      auto &value = ctx.module->params[ctx.header];
      value += '\n';
      value += expand_env_vars(line);
      break;
    }
    default: {
      parse_error("beyond the maximum indent level", num);
      return false;
    }
  }

  return true;
}

void Config::parse_error(const char *msg, int line) {
  std::cerr << "Syntax error at line " << line << ": " << msg << std::endl;
}

NS_END
