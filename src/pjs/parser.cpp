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

#include "parser.hpp"
#include "expr.hpp"

#include <map>
#include <stack>

namespace pjs {

//
// Location
//

struct Location {
  int position = 0;
  int line = 1;
  int column = 1;
};

//
// Token
//

class Token {
public:
  static Token eof;
  static Token err;

  static const int OPERATOR_BIT = (1<<31);

  static constexpr int OPR(const char *name) {
    return OPERATOR_BIT | (
      name[1] ? (
        name[2] ? (
          name[3] ? (
            name[0] | ((int)name[1] << 8) | ((int)name[2] << 16) | ((int)name[3] << 24)
          ) : (
            name[0] | ((int)name[1] << 8) | ((int)name[2] << 16)
          )
        ) : (
          name[0] | ((int)name[1] << 8)
        )
      ) : (
        name[0]
      )
    );
  }

  Token(const Token &r) : m_id(r.m_id) {}

  Token(int id) : m_id(id) {}

  Token(double n) {
    auto i = s_number_map.find(n);
    if (i == s_number_map.end()) {
      m_id = s_tokens.size();
      s_tokens.push_back({ n, "" });
      s_number_map[n] = m_id;
    } else {
      m_id = i->second;
    }
  }

  Token(const std::string &s) {
    auto i = s_string_map.find(s);
    if (i == s_string_map.end()) {
      m_id = s_tokens.size();
      s_tokens.push_back({ NAN, s });
      s_string_map[s] = m_id;
    } else {
      m_id = i->second;
    }
  }

  static void clear() {
    s_tokens.resize(1);
    s_number_map.clear();
    s_string_map.clear();
  }

  auto id() const -> int { return m_id; }
  bool is_eof() const { return !m_id; }
  bool is_operator() const { return m_id & OPERATOR_BIT; }
  bool is_number() const { return !(m_id & OPERATOR_BIT) && std::isnan(s_tokens[m_id].n) == false; }
  bool is_string() const { return !(m_id & OPERATOR_BIT) && std::isnan(s_tokens[m_id].n) == true; }
  auto n() const -> double { return s_tokens[m_id].n; }
  auto s() const -> const std::string& { return s_tokens[m_id].s; }

  bool operator==(const Token &r) const { return m_id == r.m_id; }
  bool operator!=(const Token &r) const { return m_id != r.m_id; }

  auto to_string() const -> std::string {
    if (m_id == 0) {
      return "<eof>";
    } else if (m_id == -1) {
      return "<err>";
    } else if (is_operator()) {
      char s[5] = { 0 };
      s[0] = std::toupper((m_id >>  0) & 0x7f);
      s[1] = std::toupper((m_id >>  8) & 0x7f);
      s[2] = std::toupper((m_id >> 16) & 0x7f);
      s[3] = std::toupper((m_id >> 24) & 0x7f);
      return std::string(s);
    } else if (is_number()) {
      return std::to_string(s_tokens[m_id].n);
    } else {
      return s_tokens[m_id].s;
    }
  }

private:
  int m_id;

  struct TokenData {
    double n;
    std::string s;
  };

  static std::vector<TokenData> s_tokens;
  static std::map<double, int> s_number_map;
  static std::map<std::string, int> s_string_map;
};

Token Token::eof(0);
Token Token::err(-1);
std::vector<Token::TokenData> Token::s_tokens(1);
std::map<double, int> Token::s_number_map;
std::map<std::string, int> Token::s_string_map;

//
// Tokenizer
//

class Tokenizer {
public:
  Tokenizer(const std::string &script) : m_script(script), m_token(0) {
    init_operator_map();
  }

  void set_template_mode(bool b) { m_is_template = b; }

  bool eof() const {
    return m_ptr >= m_script.length();
  }

  auto read(Location &loc) -> Token {
    peek(loc);
    m_has_peeked = false;
    loc = m_token_loc;
    return m_token;
  }

  auto peek(Location &loc) -> Token {
    if (!m_has_peeked) {
      m_token = parse(m_token_loc);
      m_has_peeked = true;
      loc = m_token_loc;
    }
    return m_token;
  }

  static bool is_identifier_name(const Token &tok, std::string &str) {
    auto i = s_identifier_names.find(tok.id());
    if (i == s_identifier_names.end()) return false;
    str = i->second;
    return true;
  }

private:
  static std::map<std::string, int> s_operator_map;
  static std::map<int, std::string> s_identifier_names;
  static void init_operator_map();

  std::string m_script;
  size_t m_ptr = 0;
  Location m_loc;
  Location m_token_loc;
  Token m_token;
  bool m_has_peeked = false;
  bool m_is_template = false;

  auto parse(Location &loc) -> Token;

  int get() const {
    return m_script[m_ptr];
  }

  void count() {
    int c = m_script[m_ptr++];
    if (c == '\n') {
      m_loc.line++;
      m_loc.column = 1;
    } else {
      m_loc.column++;
    }
    m_loc.position++;
  }

  bool is_operator_char(int c) {
    return c != '_' && c != '$' && std::ispunct(c);
  }
};

std::map<std::string, int> Tokenizer::s_operator_map;
std::map<int, std::string> Tokenizer::s_identifier_names;

void Tokenizer::init_operator_map() {
  static const char* operators[] = {
    ","     , "."     ,
    "`"     , "="     ,
    "~"     , "!"     ,
    "++"    , "--"    ,
    "+"     , "+="    ,
    "-"     , "-="    ,
    "*"     , "*="    ,
    "**"    , "**="   ,
    "/"     , "/="    ,
    "%"     , "%="    ,
    "<<"    , "<<="   ,
    ">>"    , ">>="   ,
    ">>>"   , ">>>="  ,
    "&"     , "&="    ,
    "|"     , "|="    ,
    "^"     , "^="    ,
    "&&"    , "&&="   ,
    "||"    , "||="   ,
    "??"    , "?\?="  ,
    "=="    , "==="   ,
    "!="    , "!=="   ,
    ">"     , ">="    ,
    "<"     , "<="    ,
    "?"     , ":"     , "?."    ,
    "("     , ")"     , "?.("   ,
    "["     , "]"     , "?.["   ,
    "{"     , "}"     , "..."   ,
    "=>"    , "void"  , "in"    ,
    "new"   , "delete", "typeof", "instanceof",
    "true"  , "false" , "null"  , "undefined" ,

    // Reserved keywords
    "var"   , "let"   , "const"  ,
    "if"    , "else"  , "return" ,
    "do"    , "while" , "for"    , "continue"  ,
    "switch", "case"  , "break"  , "default"   ,
    "throw" , "try"   , "catch"  , "finally"   ,
    "await" , "async" , "yield"  , "function"  ,
    "import", "export", "class"  , "package"   ,
    "with"  , "this"  , "super"  , "extends"   , "implements",
    "static", "public", "private", "protected" , "interface" ,
  };

  if (s_operator_map.empty()) {
    for (size_t i = 0; i < sizeof(operators) / sizeof(operators[0]); i++) {
      auto s = operators[i];
      if (!std::isalpha(s[0])) {
        std::string str(s);
        for (size_t i = 0; i + 1 < str.length(); i++) {
          s_operator_map[str.substr(0, i + 1)] = 0;
        }
      }
    }
    for (size_t i = 0; i < sizeof(operators) / sizeof(operators[0]); i++) {
      auto s = operators[i];
      auto id = Token::OPR(s);
      s_operator_map[s] = id;
      auto is_identifier_name = true;
      for (auto *p = s; *p; p++) is_identifier_name = is_identifier_name && std::isalpha(*p);
      if (is_identifier_name) s_identifier_names[id] = s;
    }
  }
}

auto Tokenizer::parse(Location &loc) -> Token {

  // Parse template strings
  if (m_is_template) {
    auto c = get();
    if (c == '`') {
      count();
      return Token(Token::OPR("`"));
    } else if (c == '$' && m_script[m_ptr+1] == '{') {
      count();
      count();
      return Token(Token::OPR("${"));
    }

    std::string s(1, c);
    count();
    while (!eof()) {
      auto c = get();
      if (c == '`') {
        return Token(s);
      } else if (c == '$' && m_script[m_ptr+1] == '{') {
        return Token(s);
      } else if (c == '\\') {
        s += char(c);
        count();
        if (eof()) return Token::err;
        c = get();
      }
      s += char(c);
      count();
    }
    return Token::err;

  // Parse normal script
  } else for (;;) {

    // Skip white spaces
    while (!eof()) {
      auto c = get();
      if (!std::isspace(c)) break;
      count();
    }

    // EOF?
    if (eof()) return Token::eof;

    // Get the leading char
    auto c = get();
    loc = m_loc;

    // String?
    if (c == '"' || c == '\'') {
      auto start = c;
      std::string s(1, c);
      count();
      while (!eof()) {
        auto c = get();
        if (c == start) {
          s += char(c);
          count();
          return Token(s);
        } else if (c == '\\') {
          s += char(c);
          count();
          if (eof()) return Token::err;
          c = get();
        }
        s += char(c);
        count();
      }
      return Token::err;
    }

    // Comments?
    if (c == '/') {
      auto next = m_script[m_ptr+1];
      if (next == '/') {
        count();
        count();
        while (!eof() && get() != '\n') count();
        continue;
      } else if (next == '*') {
        count();
        count();
        while (!eof()) {
          if (get() == '*') {
            if (m_script[m_ptr+1] == '/') {
              count();
              count();
              break;
            }
          }
          count();
        }
        continue;
      }
    }

    // Operator?
    if (is_operator_char(c)) {
      bool is_number = (c == '.' && std::isdigit(m_script[m_ptr+1]));
      if (!is_number) {
        std::string s, op;
        for (auto p = m_ptr; p < m_script.size(); p++) {
          auto c = m_script[p]; if (c < 0) break;
          auto k = s + char(c);
          auto i = s_operator_map.find(k);
          if (i == s_operator_map.end()) break;
          if (i->second) op = k;
          s = k;
        }
        for (size_t i = 0; i < op.length(); i++) count();
        auto i = s_operator_map.find(op);
        if (i == s_operator_map.end()) return Token::err;
        return i->second;
      }
    }

    // Number?
    if (std::isdigit(c) || c == '.') {
      std::string s(1, c);
      count();
      while (!eof()) {
        auto c = std::tolower(get());
        if (std::isdigit(c) || c=='.' || c=='e' || c=='x' || c=='o' || c=='b') {
          count();
          s += char(c);
          if (c == 'e') {
            c = get();
            if (c == '+' || c == '-') {
              s += char(c);
              count();
            }
          }
          continue;
        }
        if (std::isspace(c) || is_operator_char(c)) break;
        return Token::err;
      }
      return Token(std::stod(s)); // TODO: Handle 0oXXX and 0bXXX

    // Identifier
    } else {
      std::string s;
      while (!eof()) {
        auto c = get();
        if (std::isspace(c) || is_operator_char(c)) break;
        s += char(c);
        count();
      }
      auto i = s_operator_map.find(s);
      if (i != s_operator_map.end()) return Token(i->second);
      return Token(s);
    }
  }
}

//
// StringDecoder
//

class StringDecoder {
public:
  auto position() const -> int { return m_position; }
  auto error() const -> const std::string& { return m_error; }

  bool decode(const std::string &str, std::string &out) {
    reset(str);
    auto start = get();
    count();
    while (!eof()) {
      auto c = get();
      if (c == start) {
        return success(out);
      } else if (c == '\\') {
        count();
        if (eof()) return error(UnexpectedStringEnd);
        c = get();
        switch (c) {
          case 'b': put('\b'); count(); break;
          case 'f': put('\f'); count(); break;
          case 'n': put('\n'); count(); break;
          case 'r': put('\r'); count(); break;
          case 't': put('\t'); count(); break;
          case 'v': put('\v'); count(); break;
          case 'x': {
            count(); int h = get(); if (!std::isxdigit(h)) return error(InvalidHexChar);
            count(); int l = get(); if (!std::isxdigit(l)) return error(InvalidHexChar);
            count(); put((char)((hex(h) << 4) | hex(l))); break;
          }
          case 'u': {
            count();
            char x[4]; x[0] = get();
            if (x[0] == '{') {
              count();
              int x = 0;
              for (size_t i = 0; i < 5 && !eof(); i++) {
                c = get(); if (c == '}') break;
                if (!std::isxdigit(c)) return error(InvalidHexChar);
                x = (x << 4) | hex(c);
              }
              if (eof() || c != '}') return error(UnexpectedCodePointEnd);
              put(x);
            } else if (!std::isxdigit(x[0])) {
              return error(InvalidHexChar);
            } else {
              count(); x[1] = get(); if (!std::isxdigit(x[1])) return error(InvalidHexChar);
              count(); x[2] = get(); if (!std::isxdigit(x[2])) return error(InvalidHexChar);
              count(); x[3] = get(); if (!std::isxdigit(x[3])) return error(InvalidHexChar);
              count(); put(
                ((wchar_t)hex(x[0]) << 12)|
                ((wchar_t)hex(x[1]) <<  8)|
                ((wchar_t)hex(x[2]) <<  4)|
                ((wchar_t)hex(x[3]) <<  0)
              );
            }
            break;
          }
          default: {
            if ('0' <= c && c <= '7') {
              int o = 0;
              for (int i = 0; i < 3 && !eof(); i++) {
                c = get(); if (c < '0' || c > '7') break;
                int n = (o << 3) + (c - '0');
                if (n > 0377) break;
                o = n; count();
              }
              put(o);
            } else {
              put(c);
              count();
            }
            break;
          }
        }
      } else {
        put(c);
        count();
      }
    }
    return error(UnexpectedStringEnd);
  }

private:
  enum Error {
    UnexpectedStringEnd,
    UnexpectedCodePointEnd,
    InvalidHexChar,

  };

  std::wstring m_out;
  std::string m_error;
  std::string m_str;
  size_t m_ptr;
  int m_position;

  void reset(const std::string &str) {
    m_out.clear();
    m_error.clear();
    m_str = str;
    m_ptr = 0;
    m_position = 0;
  }

  bool eof() const {
    return (m_ptr >= m_str.length());
  }

  int get() {
    if (eof()) return 0;
    auto b = m_str[m_ptr];
    auto c = int(b) & 0xff;
    auto n = 1;
    if (b & 0x80) {
      if ((b & 0xe0) == 0xc0) { c = b & 0x1f; n = 2; } else
      if ((b & 0xf0) == 0xe0) { c = b & 0x0f; n = 3; } else
      if ((b & 0xf8) == 0xf0) { c = b & 0x07; n = 4; }
    }
    if (m_ptr + n > m_str.length()) return -1;
    for (int i = 1; i < n; i++) c = (c << 6) | (m_str[m_ptr+i] & 0x3f);
    return c;
  }

  void put(int c) {
    wchar_t buf[3];
    buf[utf16(c, buf)] = 0;
    m_out += buf;
  }

  void count() {
    if (eof()) return;
    auto b = m_str[m_ptr];
    auto n = 1;
    if (b & 0x80) {
      if ((b & 0xe0) == 0xc0) n = 2; else
      if ((b & 0xf0) == 0xe0) n = 3; else
      if ((b & 0xf8) == 0xf0) n = 4;
    }
    if (m_ptr + n < m_str.length()) {
      m_ptr += n;
      m_position++;
    }
  }

  bool success(std::string &out) {
    std::string s;
    char buf[1024]; size_t i = 0;
    for (size_t p = 0; p < m_out.length(); p++) {
      int c = m_out[p];
      if (0xd800 <= c && c <= 0xdbff) {
        c = ((c & 0x3ff) << 10) | (m_out[++p] & 0x3ff);
        c += 0x10000;
      }
      if (i + 6 > sizeof(buf)) {
        s += std::string(buf, i);
        i = 0;
      }
      i += utf8(c, buf + i);
    }
    if (i > 0) s += std::string(buf, i);
    out = s;
    return true;
  }

  bool error(Error err) {
    switch (err) {
      case UnexpectedStringEnd: m_error = "unexpected end of string"; break;
      case UnexpectedCodePointEnd: m_error = "unexpected end of code point"; break;
      case InvalidHexChar: m_error = "invalid hexadecimal character"; break;
      default: m_error = "unknown error"; break;
    }
    return false;
  }

  int hex(int c) {
    return (c >= 'a' ? (c - 'a' + 10) : (c >= 'A' ? c - 'A' + 10 : c - '0'));
  }

  auto utf8(int c, char s[]) -> size_t {
    size_t i = 0;
    if (c < 0x80) {
      s[i++] = c;
    } else if (c < 0x800) {
      s[i++] = ((c >> 6) & 0x1f) | 0xc0;
      s[i++] = ((c >> 0) & 0x3f) | 0x80;
    } else if (c < 0x10000) {
      s[i++] = ((c >> 12) & 0x0f) | 0xe0;
      s[i++] = ((c >>  6) & 0x3f) | 0x80;
      s[i++] = ((c >>  0) & 0x3f) | 0x80;
    } else {
      s[i++] = ((c >> 18) & 0x07) | 0xf0;
      s[i++] = ((c >> 12) & 0x3f) | 0x80;
      s[i++] = ((c >>  6) & 0x3f) | 0x80;
      s[i++] = ((c >>  0) & 0x3f) | 0x80;
    }
    return i;
  }

  auto utf16(int c, wchar_t s[]) -> size_t {
    size_t i = 0;
    if (c >= 0x10000) {
      c -= 0x10000;
      s[i++] = 0xd800 + (0x3ff & (c >> 10));
      s[i++] = 0xdc00 + (0x3ff & (c >>  0));
    } else {
      s[i++] = c;
    }
    return i;
  }
};

//
// ExpressionParser
//

class ExpressionParser {
public:
  ExpressionParser(const std::string &script);

  auto parse(
    std::string &error,
    int &error_line,
    int &error_column
  ) -> Expr*;

private:
  enum Error {
    UnexpectedEnd,
    UnexpectedToken,
    UnknownToken,
    UnknownOperator,
    InvalidString,
    InvalidLeftValue,
    InvalidArgumentList,
    InvalidOptionalChain,
    IncompleteExpression,
    AmbiguousPrecedence,
  };

  Tokenizer m_tokenizer;
  Location m_location;
  std::string m_error;

  Token peek() { return m_tokenizer.peek(m_location); }
  Token read() { return m_tokenizer.read(m_location); }
  Token read(Location &l) { return m_tokenizer.read(l); }

  Expr* locate(Expr *e) {
    e->locate(m_location.line, m_location.column);
    return e;
  }

  Expr* locate(Expr *e, const Location &l) {
    e->locate(l.line, l.column);
    return e;
  }

  Expr* locate(Expr *e, Expr *l) {
    e->locate(l->line(), l->column());
    return e;
  }

  Expr* expression(bool no_comma = false);
  Expr* operand();

  bool precedes(Token a, Token b) {
    auto i = s_precedence_table.find(a.id());
    auto j = s_precedence_table.find(b.id());
    if (i == s_precedence_table.end()) return false;
    if (j == s_precedence_table.end()) return true;
    auto pa = std::abs(i->second);
    auto pb = std::abs(j->second);
    if (pa > pb) return true;
    if (pa < pb) return false;
    if (i->second < 0) return false; // right-to-left
    return true;
  }

  Expr* error(Error err) {
    switch (err) {
      case UnexpectedEnd: m_error = "unexpected end of expression"; break;
      case UnexpectedToken: m_error = "unexpected token"; break;
      case UnknownToken: m_error = "unknown token"; break;
      case UnknownOperator: m_error = "unknown operator"; break;
      case InvalidString: m_error = "invalid string encoding"; break;
      case InvalidLeftValue: m_error = "invalid left-value"; break;
      case InvalidArgumentList: m_error = "invalid argument list"; break;
      case InvalidOptionalChain: m_error = "invalid optional chain"; break;
      case IncompleteExpression: m_error = "incomplete expression"; break;
      case AmbiguousPrecedence: m_error = "ambiguous exponentiation precedence"; break;
    }
    return nullptr;
  }

  static std::unordered_map<int, int> s_precedence_table;
};

// Operator precedence table as in:
// https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/Operator_Precedence#table

std::unordered_map<int, int> ExpressionParser::s_precedence_table = {
  { Token::OPR("."   ), 20 },
  { Token::OPR("["   ), 20 },
  { Token::OPR("("   ), 20 },
  { Token::OPR("?."  ), 20 },
  { Token::OPR("?.[" ), 20 },
  { Token::OPR("?.(" ), 20 },
  { Token::OPR("new" ), 19 },
  { Token::OPR("x++" ), 18 },
  { Token::OPR("x--" ), 18 },
  { Token::OPR("!"   ), -17 },
  { Token::OPR("~"   ), -17 },
  { Token::OPR("+x"  ), -17 },
  { Token::OPR("-x"  ), -17 },
  { Token::OPR("++x" ), -17 },
  { Token::OPR("--x" ), -17 },
  { Token::OPR("void"), -17 },
  { Token::OPR("typeof"), -17 },
  { Token::OPR("delete"), -17 },
  { Token::OPR("**"  ), -16 },
  { Token::OPR("*"   ), 15 },
  { Token::OPR("/"   ), 15 },
  { Token::OPR("%"   ), 15 },
  { Token::OPR("+"   ), 14 },
  { Token::OPR("-"   ), 14 },
  { Token::OPR("<<"  ), 13 },
  { Token::OPR(">>"  ), 13 },
  { Token::OPR(">>>" ), 13 },
  { Token::OPR("<"   ), 12 },
  { Token::OPR("<="  ), 12 },
  { Token::OPR(">"   ), 12 },
  { Token::OPR(">="  ), 12 },
  { Token::OPR("in"  ), 12 },
  { Token::OPR("instanceof"), 12 },
  { Token::OPR("=="  ), 11 },
  { Token::OPR("===" ), 11 },
  { Token::OPR("!="  ), 11 },
  { Token::OPR("!==" ), 11 },
  { Token::OPR("&"   ), 10 },
  { Token::OPR("^"   ),  9 },
  { Token::OPR("|"   ),  8 },
  { Token::OPR("&&"  ),  7 },
  { Token::OPR("||"  ),  6 },
  { Token::OPR("??"  ),  5 },
  { Token::OPR("?"   ), -4 },
  { Token::OPR(":"   ), -4 },
  { Token::OPR("="   ), -3 },
  { Token::OPR("+="  ), -3 },
  { Token::OPR("-="  ), -3 },
  { Token::OPR("*="  ), -3 },
  { Token::OPR("/="  ), -3 },
  { Token::OPR("%="  ), -3 },
  { Token::OPR("**=" ), -3 },
  { Token::OPR("<<=" ), -3 },
  { Token::OPR(">>=" ), -3 },
  { Token::OPR(">>>="), -3 },
  { Token::OPR("&="  ), -3 },
  { Token::OPR("|="  ), -3 },
  { Token::OPR("^="  ), -3 },
  { Token::OPR("&&=" ), -3 },
  { Token::OPR("||=" ), -3 },
  { Token::OPR("?\?="), -3 },
  { Token::OPR(","   ),  1 },
};

ExpressionParser::ExpressionParser(const std::string &script) : m_tokenizer(script)
{
}

auto ExpressionParser::parse(
  std::string &error,
  int &error_line,
  int &error_column) -> Expr*
{
  auto e = expression();
  if (!e) {
    error = m_error;
    error_line = m_location.line;
    error_column = m_location.column;
    return nullptr;
  }
  return e;
}

Expr* ExpressionParser::expression(bool no_comma) {
  std::stack<Token> operators;
  std::stack<Location> locations;
  std::stack<std::unique_ptr<Expr>> operands;

  // Do at least once and repeat until stack is empty
  do {

    // When the last operator is parenthesis-like
    int last_operator = operators.empty() ? 0 : operators.top().id();
    if (last_operator == Token::OPR("("  ) ||
        last_operator == Token::OPR("["  ) ||
        last_operator == Token::OPR("?.(") ||
        last_operator == Token::OPR("?.["))
    {
      auto is_call = (last_operator == Token::OPR("(") || last_operator == Token::OPR("?.("));
      Token closing(is_call ? Token::OPR(")") : Token::OPR("]"));

      // Parse arguments
      std::vector<std::unique_ptr<Expr>> argv;
      for (;;) {
        auto t = peek();
        if (t == Token::eof) return error(UnexpectedEnd);
        if (t == Token::err) return error(UnknownToken);
        if (t == closing) break;
        auto e = expression(is_call);
        if (!e) return nullptr;
        argv.push_back(std::unique_ptr<Expr>(e));
        if (!is_call) break;
        t = peek();
        if (t.id() == Token::OPR(",")) read();
      }

      // Check closing parenthesis
      auto t = peek();
      if (t == Token::eof) return error(UnexpectedEnd);
      if (t == Token::err) return error(UnknownToken);
      if (t == closing) read();
      else return error(UnexpectedToken);

      // Make the operand
      operators.pop();
      auto l = locations.top(); locations.pop();
      auto o = operands.top().release(); operands.pop();
      auto is_new = (!operators.empty() && operators.top().id() == Token::OPR("new"));
      if (is_call && is_new) {
        if (last_operator == Token::OPR("?.(")) return error(InvalidOptionalChain);
        auto e = construct(o, std::move(argv));
        operators.pop(); // pop 'new'
        locations.pop();
        operands.pop(); // pop the empty operand
        operands.push(std::unique_ptr<Expr>(locate(e, l)));
      } else if (is_call) {
        auto e = (last_operator == Token::OPR("?.(") ? opt_call(o, std::move(argv)) : call(o, std::move(argv)));
        operands.push(std::unique_ptr<Expr>(locate(e, l)));
      } else if (argv.size() != 1) {
        return error(UnexpectedToken);
      } else {
        auto k = argv[0].release();
        auto e = (last_operator == Token::OPR("?.[") ? opt_prop(o, k) : prop(o, k));
        operands.push(std::unique_ptr<Expr>(locate(e, l)));
      }

    // When the last operator is dot-like
    } else if (
      last_operator == Token::OPR(".") ||
      last_operator == Token::OPR("?.")
    ) {
      auto t = peek();
      std::string str;
      if (t.is_string() && t.s()[0] != '"' && t.s()[0] != '\'') {
        read();
        operands.push(std::unique_ptr<Expr>(locate(identifier(t.s()))));
      } else if (t.is_operator() && Tokenizer::is_identifier_name(t, str)) {
        read();
        operands.push(std::unique_ptr<Expr>(locate(identifier(str))));
      } else {
        return error(UnexpectedToken);
      }

    // Parse the operand within the current nesting level
    } else {

      // Parse unary operators
      for (;;) {
        Location l;
        auto t = peek();
        if (t == Token::eof) return error(UnexpectedEnd);
        if (t == Token::err) return error(UnknownToken);
        switch (t.id()) {
          case Token::OPR("+"):
            read(l);
            operands.push(nullptr);
            operators.push(Token::OPR("+x"));
            locations.push(l);
            continue;
          case Token::OPR("-"):
            read(l);
            operands.push(nullptr);
            operators.push(Token::OPR("-x"));
            locations.push(l);
            continue;
          case Token::OPR("++"):
            read(l);
            operands.push(nullptr);
            operators.push(Token::OPR("++x"));
            locations.push(l);
            continue;
          case Token::OPR("--"):
            read(l);
            operands.push(nullptr);
            operators.push(Token::OPR("--x"));
            locations.push(l);
            continue;
          case Token::OPR("~"):
          case Token::OPR("!"):
          case Token::OPR("void"):
          case Token::OPR("typeof"):
          case Token::OPR("new"):
          case Token::OPR("delete"):
            operands.push(nullptr);
            operators.push(read(l));
            locations.push(l);
            continue;
        }
        break;
      }

      // Trailing comma of a list
      auto t = peek();
      if (t.id() == Token::OPR(")") &&
          operators.size() > 0 &&
          operators.top().id() == Token::OPR(",")
      ) {
        operands.push(nullptr);
      } else {

        // Parse an operand
        auto e = operand();
        if (!e) return nullptr;

        // Push the operand to stack
        operands.push(std::unique_ptr<Expr>(e));
      }
    }

    // Parse operators
    for (;;) {
      auto t = peek();
      auto is_end = (
        (t == Token::eof) ||
        (t.id() == Token::OPR(")")) ||
        (t.id() == Token::OPR("]")) ||
        (t.id() == Token::OPR("}")) ||
        (t.id() == Token::OPR(",") && no_comma)
      );
      if (t == Token::err) return error(UnknownToken);
      if (!is_end && !t.is_operator()) return error(UnexpectedToken);

      // Some operators are invalid here, post operators are converted
      switch (t.id()) {
        case Token::OPR("~"):
        case Token::OPR("!"):
        case Token::OPR("void"):
        case Token::OPR("typeof"):
        case Token::OPR("new"):
        case Token::OPR("delete"):
          return error(UnexpectedToken);
        case Token::OPR("++"):
          t = Token(Token::OPR("x++"));
          break;
        case Token::OPR("--"):
          t = Token(Token::OPR("x--"));
          break;
      }

      // Reduce earlier operators
      while (!operators.empty() && (is_end || precedes(operators.top(), t))) {
        Expr *e = nullptr;
        auto b = operands.top().release(); operands.pop();
        auto a = operands.top().release(); operands.pop();
        auto o = operators.top(); operators.pop();
        auto l = locations.top(); locations.pop();
        if (t.id() == Token::OPR("**")) {
          if (o.id() == Token::OPR("~" ) ||
              o.id() == Token::OPR("!" ) ||
              o.id() == Token::OPR("+x") ||
              o.id() == Token::OPR("-x")
          ) {
            return error(AmbiguousPrecedence);
          }
        }
        switch (o.id()) {
          case Token::OPR("instanceof"): e = instance_of(a, b); break;
          case Token::OPR("typeof"): e = type_of(b); break;
          case Token::OPR("new"): e = construct(b); break;
          case Token::OPR("delete"): e = del(b); break;
          case Token::OPR("void"): e = discard(b); break;
          case Token::OPR("+x"  ): e = pos(b); break;
          case Token::OPR("-x"  ): e = neg(b); break;
          case Token::OPR("~"   ): e = bit_not(b); break;
          case Token::OPR("!"   ): e = bool_not(b); break;
          case Token::OPR("+"   ): e = add(a, b); break;
          case Token::OPR("-"   ): e = sub(a, b); break;
          case Token::OPR("*"   ): e = mul(a, b); break;
          case Token::OPR("**"  ): e = pow(a, b); break;
          case Token::OPR("/"   ): e = div(a, b); break;
          case Token::OPR("%"   ): e = rem(a, b); break;
          case Token::OPR("<<"  ): e = shl(a, b); break;
          case Token::OPR(">>"  ): e = shr(a, b); break;
          case Token::OPR(">>>" ): e = usr(a, b); break;
          case Token::OPR("&"   ): e = bit_and(a, b); break;
          case Token::OPR("|"   ): e = bit_or(a, b); break;
          case Token::OPR("^"   ): e = bit_xor(a, b); break;
          case Token::OPR("&&"  ): e = bool_and(a, b); break;
          case Token::OPR("||"  ): e = bool_or(a, b); break;
          case Token::OPR("??"  ): e = null_or(a, b); break;
          case Token::OPR("=="  ): e = eql(a, b); break;
          case Token::OPR("!="  ): e = neq(a, b); break;
          case Token::OPR("===" ): e = same(a, b); break;
          case Token::OPR("!==" ): e = diff(a, b); break;
          case Token::OPR(">"   ): e = gt(a, b); break;
          case Token::OPR(">="  ): e = ge(a, b); break;
          case Token::OPR("<"   ): e = lt(a, b); break;
          case Token::OPR("<="  ): e = le(a, b); break;
          case Token::OPR("in"  ): e = in(a, b); break;
          case Token::OPR("="   ): e = assign(a, b); break;
          case Token::OPR("+="  ): e = add_assign(a, b); break;
          case Token::OPR("-="  ): e = sub_assign(a, b); break;
          case Token::OPR("*="  ): e = mul_assign(a, b); break;
          case Token::OPR("/="  ): e = div_assign(a, b); break;
          case Token::OPR("%="  ): e = rem_assign(a, b); break;
          case Token::OPR("**=" ): e = pow_assign(a, b); break;
          case Token::OPR("<<=" ): e = shl_assign(a, b); break;
          case Token::OPR(">>=" ): e = shr_assign(a, b); break;
          case Token::OPR(">>>="): e = usr_assign(a, b); break;
          case Token::OPR("&="  ): e = bit_and_assign(a, b); break;
          case Token::OPR("|="  ): e = bit_or_assign(a, b); break;
          case Token::OPR("^="  ): e = bit_xor_assign(a, b); break;
          case Token::OPR("&&=" ): e = bool_and_assign(a, b); break;
          case Token::OPR("||=" ): e = bool_or_assign(a, b); break;
          case Token::OPR("?\?="): e = null_or_assign(a, b); break;
          case Token::OPR("x++" ): e = post_inc(a); break;
          case Token::OPR("x--" ): e = post_dec(a); break;
          case Token::OPR("++x" ): e = pre_inc(b); break;
          case Token::OPR("--x" ): e = pre_dec(b); break;
          case Token::OPR(","   ): e = compound(a, b); break;
          case Token::OPR("."   ):
          case Token::OPR("?."  ): {
            auto s = identifier_to_string(b);
            if (!s) {
              delete a;
              delete b;
              return error(UnexpectedToken);
            }
            e = (o.id() == Token::OPR("?.") ? opt_prop(a, s) : prop(a, s));
            break;
          }
          case Token::OPR(":"): {
            if (operators.empty() || operators.top().id() != Token::OPR("?")) {
              return error(UnexpectedToken);
            }
            auto c = operands.top().release();
            auto l = locations.top();
            operands.pop();
            operators.pop();
            locations.pop();
            e = select(c, a, b);
            break;
          }
          case Token::OPR("?"): {
            return error(UnexpectedToken);
          }
          default: {
            delete a;
            delete b;
            return error(UnknownOperator);
          }
        }
        operands.push(std::unique_ptr<Expr>(locate(e, l)));
      }

      // Push the (potentially converted) operator in stack
      if (!is_end) {
        Location l;
        read(l);
        operators.push(t);
        locations.push(l);
      }

      // When it is a post unary operator, continue on with an empty second operand
      if (t.id() == Token::OPR("x++") || t.id() == Token::OPR("x--")) {
        operands.push(nullptr);
        continue;
      }
      break;
    }
  } while (!operators.empty());

  return operands.top().release();
}

Expr* ExpressionParser::operand() {
  auto t = peek();
  if (t.id() == Token::OPR("(")) {
    Location l;
    read(l);
    if (peek().id() == Token::OPR(")")) {
      read();
      if (peek().id() != Token::OPR("=>")) {
        return error(UnexpectedToken); // must be an arrow function
      }
      read();
      if (peek().id() == Token::OPR("{")) {
        return error(UnexpectedToken); // function body statements are not supported
      }
      auto f = expression(true);
      if (!f) return nullptr;
      return locate(function(nullptr, f), l);
    }
    auto e = expression();
    if (!e) return nullptr;
    auto t = peek();
    if (t == Token::eof) return error(UnexpectedEnd);
    if (t == Token::err) return error(UnknownToken);
    if (t.id() != Token::OPR(")")) return error(UnexpectedToken);
    Location loc;
    read(loc);

    // Could it be an arrow function?
    if (peek().id() == Token::OPR("=>")) {
      if (!e->is_argument() && !e->is_argument_list()) {
        return error(InvalidArgumentList);
      }
      read();
      if (peek().id() == Token::OPR("{")) {
        return error(UnexpectedToken); // function body statements are not supported
      }
      auto f = expression(true);
      if (!f) return nullptr;
      return locate(function(e, f), l);
    }

    if (e->is_comma_ended()) {
      m_location = loc;
      return error(IncompleteExpression);
    }

    return e;
  }

  if (t.id() == Token::OPR("`")) {
    Location l;
    read(l);
    std::list<std::unique_ptr<Expr>> list;
    m_tokenizer.set_template_mode(true);
    for (;;) {
      auto t = peek();
      if (t == Token::eof) return error(UnexpectedEnd);
      if (t == Token::err) return error(UnknownToken);
      if (t.id() == Token::OPR("`")) {
        read();
        break;
      } else if (t.id() == Token::OPR("${")) {
        read();
        m_tokenizer.set_template_mode(false);
        auto e = expression();
        if (!e) return nullptr;
        list.push_back(std::unique_ptr<Expr>(e));
        auto t = peek();
        if (t == Token::eof) return error(UnexpectedEnd);
        if (t == Token::err) return error(UnknownToken);
        if (t.id() != Token::OPR("}")) return error(UnexpectedToken);
        read();
        m_tokenizer.set_template_mode(true);
      } else if (t.is_string()) {
        Location l;
        read(l);
        std::string s(1, '`');
        s += t.s();
        s += '`';
        std::string str;
        StringDecoder decoder;
        if (!decoder.decode(s, str)) {
          return error(InvalidString);
        }
        list.push_back(std::unique_ptr<Expr>(locate(string(str), l)));
      } else {
        return error(UnexpectedToken);
      }
    }
    m_tokenizer.set_template_mode(false);
    return locate(concat(std::move(list)), l);
  }

  switch (t.id()) {
    case Token::OPR("undefined"): read(); return locate(undefined());
    case Token::OPR("null"): read(); return locate(null());
    case Token::OPR("false"): read(); return locate(boolean(false));
    case Token::OPR("true"): read(); return locate(boolean(true));
  }

  if (t.is_number()) {
    read();
    return locate(number(t.n()));
  }

  if (t.is_string()) {
    Location l;
    read(l);
    auto start = t.s()[0];
    if (start == '"' || start == '\'') {
      std::string str;
      StringDecoder decoder;
      if (!decoder.decode(t.s(), str)) {
        return error(InvalidString);
      }
      return locate(string(str), l);
    } else {
      auto e = locate(identifier(t.s()));
      if (peek().id() == Token::OPR("=>")) {
        read();
        if (peek().id() == Token::OPR("{")) {
          return error(UnexpectedToken); // function body statements are not supported
        }
        auto f = expression(true);
        if (!f) return nullptr;
        e = locate(function(e, f), l);
      }
      return e;
    }
  }

  if (t.id() == Token::OPR("{")) {
    Location l;
    read(l);
    std::list<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> list;
    for (;;) {
      auto t = peek();
      if (t.id() == Token::OPR("}")) {
        read();
        break;
      }
      std::string key;
      Expr* k = nullptr;
      Expr* v = nullptr;
      if (t.is_string()) {
        read();
        auto start = t.s()[0];
        if (start == '"' || start == '\'') {
          std::string str;
          StringDecoder decoder;
          if (!decoder.decode(t.s(), str)) return error(InvalidString);
          k = locate(string(str));
        } else {
          key = t.s();
          k = locate(string(key));
        }
      } else if (t.is_number()) {
        read();
        k = locate(string(std::to_string(t.n())));
      } else if (t.id() == Token::OPR("[")) {
        read();
        k = expression();
        if (!k) return nullptr;
        auto t = peek();
        if (t.id() != Token::OPR("]")) return error(UnexpectedToken);
        read();
      } else if (t.id() == Token::OPR("...")) {
        read();
        v = expression(true);
        if (!v) return nullptr;
      } else if (Tokenizer::is_identifier_name(t, key)) {
        read();
        k = locate(string(key));
      } else {
        return error(UnexpectedToken);
      }
      if (!v) {
        auto t = peek();
        if (!key.empty() && (t.id() == Token::OPR(",") || t.id() == Token::OPR("}"))) {
          v = locate(identifier(key), k);
        } else if (t.id() == Token::OPR(":")) {
          read();
          v = expression(true);
          if (!v) return nullptr;
        } else {
          delete k;
          return error(UnexpectedToken);
        }
      }
      list.push_back({
        std::unique_ptr<Expr>(k),
        std::unique_ptr<Expr>(v),
      });
      t = peek();
      if (t.id() == Token::OPR(",")) read();
    }
    return locate(object(std::move(list)), l);
  }

  if (t.id() == Token::OPR("[")) {
    Location l;
    read(l);
    std::list<std::unique_ptr<Expr>> list;
    for (;;) {
      auto t = peek();
      if (t.id() == Token::OPR("]")) {
        read();
        break;
      }
      if (t.id() == Token::OPR("...")) {
        read();
        auto v = expression(true);
        if (!v) return nullptr;
        list.push_back(std::unique_ptr<Expr>(expand(v)));
      } else {
        auto v = expression(true);
        if (!v) return nullptr;
        list.push_back(std::unique_ptr<Expr>(v));
      }
      t = peek();
      if (t.id() == Token::OPR(",")) read();
    }
    return locate(array(std::move(list)), l);
  }

  return error(UnexpectedToken);
}

//
// Parser
//

auto Parser::parse(
  const std::string &script,
  std::string &error,
  int &error_line,
  int &error_column) -> Expr*
{
  Token::clear();
  ExpressionParser parser(script);
  return parser.parse(error, error_line, error_column);
}

auto Parser::tokenize(const std::string &script) -> std::list<std::string> {
  std::list<std::string> tokens;
  Token::clear();
  Tokenizer tokenizer(script);
  while (!tokenizer.eof()) {
    Location loc;
    auto t = tokenizer.read(loc);
    tokens.push_back(t.to_string());
    if (t == Token::err) break;
  }
  return tokens;
}

} // namespace pjs
