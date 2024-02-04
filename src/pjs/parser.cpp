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
#include "stmt.hpp"

#include <map>
#include <mutex>
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
  static const Token eof;
  static const Token err;

  static const int OPERATOR_BIT = (1<<31);

  static constexpr int ID(const char *name) {
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

  thread_local static std::vector<TokenData> s_tokens;
  thread_local static std::map<double, int> s_number_map;
  thread_local static std::map<std::string, int> s_string_map;
};

const Token Token::eof(0);
const Token Token::err(-1);
thread_local std::vector<Token::TokenData> Token::s_tokens(1);
thread_local std::map<double, int> Token::s_number_map;
thread_local std::map<std::string, int> Token::s_string_map;

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

  bool peek_eol() {
    if (m_is_template) return false;
    return parse_space();
  }

  static bool is_identifier_name(const Token &tok, std::string &str) {
    auto i = s_identifier_names.find(tok.id());
    if (i == s_identifier_names.end()) return false;
    str = i->second;
    return true;
  }

private:
  static std::mutex s_init_operator_map_mutex;
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
  bool parse_space();

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

std::mutex Tokenizer::s_init_operator_map_mutex;
std::map<std::string, int> Tokenizer::s_operator_map;
std::map<int, std::string> Tokenizer::s_identifier_names;

void Tokenizer::init_operator_map() {
  std::lock_guard<std::mutex> lock(s_init_operator_map_mutex);

  static const char* operators[] = {
    ","     , ";"     ,
    "."     , "="     ,
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
    "=>"    , "`"     , "new"   , "delete"    ,
    "void"  , "in"    , "typeof", "instanceof",
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
      auto id = Token::ID(s);
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
      return Token(Token::ID("`"));
    } else if (c == '$' && m_script[m_ptr+1] == '{') {
      count();
      count();
      return Token(Token::ID("${"));
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
  } else {

    // Skip white spaces
    parse_space();

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
        if ((c=='.' || std::isdigit(c)) ||
            (c=='x' || c=='X') ||
            (c=='o' || c=='O') ||
            ('a' <= c && c <= 'f') ||
            ('A' <= c && c <= 'F')
        ) {
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
      if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B' || s[1] == 'o' || s[1] == 'O')) {
        uint64_t n = 0;
        int pow = (s[1] == 'b' || s[1] == 'B' ? 1 : 3);
        int base = 1 << pow;
        for (size_t i = 2; i < s.length(); i++) {
          int c = s[i] - '0';
          if (c < 0 || c >= base) return Token::err;
          n = (n << pow) + c;
        }
        return Token(double(n));
      } else {
        char *endp = nullptr;
        auto n = std::strtod(s.c_str(), &endp);
        if (endp - s.c_str() < s.length()) return Token::err;
        return Token(n);
      }

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

bool Tokenizer::parse_space() {
  bool has_eol = false;
  for (;;) {

    // Skip white spaces
    while (!eof()) {
      auto c = get();
      if (c == '\n') has_eol = true;
      if (!std::isspace(c)) break;
      count();
    }

    // EOF?
    if (eof()) return has_eol;

    // Comments?
    if (get() == '/') {
      auto next = m_script[m_ptr+1];
      if (next == '/') {
        count();
        count();
        while (!eof() && get() != '\n') count();
        has_eol = true;
        continue;
      } else if (next == '*') {
        count();
        count();
        while (!eof()) {
          auto c = get();
          if (c == '\n') has_eol = true;
          if (c == '*') {
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

    return has_eol;
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
// ScriptParser
//

class ScriptParser {
public:
  ScriptParser(const Source *source);

  auto parse(
    std::string &error,
    int &error_line,
    int &error_column
  ) -> Stmt*;

  auto parse_expr(
    std::string &error,
    int &error_line,
    int &error_column
  ) -> Expr*;

private:
  enum Error {
    UnexpectedEOF,
    UnexpectedEOL,
    UnexpectedToken,
    UnknownToken,
    UnknownOperator,
    InvalidString,
    InvalidLeftValue,
    InvalidArgumentList,
    InvalidOptionalChain,
    IncompleteExpression,
    AmbiguousPrecedence,
    TokenExpected,
    CaseExpected,
    MissingIdentifier,
    MissingExpression,
    MissingCatchFinally,
    DuplicatedDefault,
  };

  const Source* m_source;
  Tokenizer m_tokenizer;
  Location m_location;
  std::string m_error;

  Token peek() { return m_tokenizer.peek(m_location); }
  Token read() { return m_tokenizer.read(m_location); }
  Token read(Location &l) { return m_tokenizer.read(l); }

  Expr* locate(Expr *e) {
    locate(e, m_location);
    return e;
  }

  Expr* locate(Expr *e, const Location &l) {
    e->locate(m_source, l.line, l.column);
    return e;
  }

  Expr* locate(Expr *e, Expr *l) {
    e->locate(l->source(), l->line(), l->column());
    return e;
  }

  Stmt* statement_block();
  Stmt* statement();
  Expr* expression(bool no_comma = false);
  Expr* operand();
  Expr* block_function(Location &loc);
  Expr* arrow_function(Location &loc, Expr *arguments);

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

  bool peek(int token) {
    return peek().id() == token;
  }

  bool peek(int token, Error err) {
    if (peek().id() == token) return true;
    error(err, token);
    return false;
  }

  bool peek_eol() {
    return m_tokenizer.peek_eol();
  }

  bool peek_eol(Error err) {
    if (!peek_eol()) return false;
    error(err);
    return true;
  }

  bool peek_end() {
    auto t = peek();
    if (t == Token::eof) return true;
    if (t.id() == Token::ID("case")) return true;
    if (t.id() == Token::ID("default")) return true;
    if (t.id() == Token::ID("}")) return true;
    if (t.id() == Token::ID(";")) {
      read();
      return true;
    }
    return false;
  }

  bool peek_end(Error err) {
    if (!peek_end()) return false;
    error(err);
    return true;
  }

  bool read(int token) {
    if (peek().id() == token) {
      read();
      return true;
    } else {
      return false;
    }
  }

  bool read(int token, Error err) {
    if (read(token)) return true;
    error(err, token);
    return false;
  }

  auto read_identifier() -> std::string {
    auto t = peek();
    if (!t.is_string() || t.s()[0] == '"' || t.s()[0] == '\'') {
      return std::string();
    }
    read();
    return t.s();
  }

  auto read_identifier(Error err) -> std::string {
    auto id = read_identifier();
    if (id.empty()) error(err);
    return id;
  }

  void read_semicolons() {
    while (peek().id() == Token::ID(";")) {
      read();
    }
  }

  bool has_error() const {
    return !m_error.empty();
  }

  Expr* error(Error err, int token = 0) {
    char tok[6];
    tok[0] = '\'';
    tok[1] = 255 & (token >>  0);
    tok[2] = 255 & (token >>  8);
    tok[3] = 255 & (token >> 16);
    tok[4] = 255 & (token >> 24);
    tok[5] = 0;
    switch (err) {
      case UnexpectedEOF: m_error = "unexpected end of expression"; break;
      case UnexpectedEOL: m_error = "unexpected end of line"; break;
      case UnexpectedToken: m_error = "unexpected token"; break;
      case UnknownToken: m_error = "unknown token"; break;
      case UnknownOperator: m_error = "unknown operator"; break;
      case InvalidString: m_error = "invalid string encoding"; break;
      case InvalidLeftValue: m_error = "invalid left-value"; break;
      case InvalidArgumentList: m_error = "invalid argument list"; break;
      case InvalidOptionalChain: m_error = "invalid optional chain"; break;
      case IncompleteExpression: m_error = "incomplete expression"; break;
      case AmbiguousPrecedence: m_error = "ambiguous exponentiation precedence"; break;
      case TokenExpected: m_error = std::string(tok) + "' expected"; break;
      case CaseExpected: m_error = "case or default clause expected"; break;
      case MissingIdentifier: m_error = "missing identifier"; break;
      case MissingExpression: m_error = "missing expression"; break;
      case MissingCatchFinally: m_error = "missing catch or finally"; break;
      case DuplicatedDefault: m_error = "duplicated default clause"; break;
    }
    return nullptr;
  }

  static const std::unordered_map<int, int> s_precedence_table;
};

// Operator precedence table as in:
// https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/Operator_Precedence#table

const std::unordered_map<int, int> ScriptParser::s_precedence_table = {
  { Token::ID("."   ), 20 },
  { Token::ID("["   ), 20 },
  { Token::ID("("   ), 20 },
  { Token::ID("?."  ), 20 },
  { Token::ID("?.[" ), 20 },
  { Token::ID("?.(" ), 20 },
  { Token::ID("new" ), 19 },
  { Token::ID("x++" ), 18 },
  { Token::ID("x--" ), 18 },
  { Token::ID("!"   ), -17 },
  { Token::ID("~"   ), -17 },
  { Token::ID("+x"  ), -17 },
  { Token::ID("-x"  ), -17 },
  { Token::ID("++x" ), -17 },
  { Token::ID("--x" ), -17 },
  { Token::ID("void"), -17 },
  { Token::ID("typeof"), -17 },
  { Token::ID("delete"), -17 },
  { Token::ID("**"  ), -16 },
  { Token::ID("*"   ), 15 },
  { Token::ID("/"   ), 15 },
  { Token::ID("%"   ), 15 },
  { Token::ID("+"   ), 14 },
  { Token::ID("-"   ), 14 },
  { Token::ID("<<"  ), 13 },
  { Token::ID(">>"  ), 13 },
  { Token::ID(">>>" ), 13 },
  { Token::ID("<"   ), 12 },
  { Token::ID("<="  ), 12 },
  { Token::ID(">"   ), 12 },
  { Token::ID(">="  ), 12 },
  { Token::ID("in"  ), 12 },
  { Token::ID("instanceof"), 12 },
  { Token::ID("=="  ), 11 },
  { Token::ID("===" ), 11 },
  { Token::ID("!="  ), 11 },
  { Token::ID("!==" ), 11 },
  { Token::ID("&"   ), 10 },
  { Token::ID("^"   ),  9 },
  { Token::ID("|"   ),  8 },
  { Token::ID("&&"  ),  7 },
  { Token::ID("||"  ),  6 },
  { Token::ID("??"  ),  5 },
  { Token::ID("?"   ), -4 },
  { Token::ID(":"   ), -4 },
  { Token::ID("="   ), -3 },
  { Token::ID("+="  ), -3 },
  { Token::ID("-="  ), -3 },
  { Token::ID("*="  ), -3 },
  { Token::ID("/="  ), -3 },
  { Token::ID("%="  ), -3 },
  { Token::ID("**=" ), -3 },
  { Token::ID("<<=" ), -3 },
  { Token::ID(">>=" ), -3 },
  { Token::ID(">>>="), -3 },
  { Token::ID("&="  ), -3 },
  { Token::ID("|="  ), -3 },
  { Token::ID("^="  ), -3 },
  { Token::ID("&&=" ), -3 },
  { Token::ID("||=" ), -3 },
  { Token::ID("?\?="), -3 },
  { Token::ID(","   ),  1 },
};

ScriptParser::ScriptParser(const Source *source)
  : m_source(source)
  , m_tokenizer(source->content)
{
}

auto ScriptParser::parse(
  std::string &error,
  int &error_line,
  int &error_column) -> Stmt*
{
  std::list<std::unique_ptr<Stmt>> list;
  while (peek() != Token::eof) {
    auto s = statement();
    if (!s) {
      error = m_error;
      error_line = m_location.line;
      error_column = m_location.column;
      return nullptr;
    }
    list.push_back(std::unique_ptr<Stmt>(s));
  }
  return block(std::move(list));
}

auto ScriptParser::parse_expr(
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

Stmt* ScriptParser::statement_block() {
  std::list<std::unique_ptr<Stmt>> stmts;
  while (!peek_end()) {
    auto s = statement();
    if (!s) return nullptr;
    stmts.push_back(std::unique_ptr<Stmt>(s));
  }
  return block(std::move(stmts));
}

Stmt* ScriptParser::statement() {
  switch (peek().id()) {
    case Token::ID(";"):
      read_semicolons();
      return block();
    case Token::ID("{"): {
      read();
      auto s = std::unique_ptr<Stmt>(statement_block());
      if (!s) return nullptr;
      if (!read(Token::ID("}"))) return nullptr;
      return s.release();
    }
    case Token::ID("var"): {
      read();
      if (peek_eol(MissingIdentifier)) return nullptr;
      auto name = read_identifier(MissingIdentifier);
      if (name.empty()) return nullptr;
      if (read(Token::ID("="))) {
        auto e = expression();
        if (!e) return nullptr;
        read(Token::ID(";"));
        return var(name, e);
      }
      read_semicolons();
      return var(name);
    }
    case Token::ID("function"): {
      error(UnexpectedToken);
      return nullptr;
    }
    case Token::ID("if"): {
      read();
      if (!read(Token::ID("("), TokenExpected)) return nullptr;
      auto cond = std::unique_ptr<Expr>(expression());
      if (!cond) return nullptr;
      if (!read(Token::ID(")"), TokenExpected)) return nullptr;
      auto then_clause = std::unique_ptr<Stmt>(statement());
      if (!then_clause) return nullptr;
      if (read(Token::ID("else"))) {
        auto else_clause = std::unique_ptr<Stmt>(statement());
        if (!else_clause) return nullptr;
        return if_else(cond.release(), then_clause.release(), else_clause.release());
      } else {
        return if_else(cond.release(), then_clause.release());
      }
    }
    case Token::ID("switch"): {
      read();
      if (!read(Token::ID("("), TokenExpected)) return nullptr;
      auto cond = std::unique_ptr<Expr>(expression());
      if (!cond) return nullptr;
      if (!read(Token::ID(")"), TokenExpected)) return nullptr;
      if (!read(Token::ID("{"), TokenExpected)) return nullptr;
      std::list<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Stmt>>> cases;
      std::unique_ptr<Stmt> default_case;
      while (!read(Token::ID("}"))) {
        if (read(Token::ID("default"))) {
          if (!read(Token::ID(":"), TokenExpected)) return nullptr;
          if (default_case) { error(DuplicatedDefault); return nullptr; }
          auto s = statement_block();
          if (!s) return nullptr;
          default_case = std::unique_ptr<Stmt>(s);
        } else if (read(Token::ID("case"))) {
          auto e = std::unique_ptr<Expr>(expression());
          if (!e) return nullptr;
          if (!read(Token::ID(":"), TokenExpected)) return nullptr;
          auto s = std::unique_ptr<Stmt>(statement_block());
          if (!s) return nullptr;
          cases.push_back(std::make_pair(std::move(e), std::move(s)));
        } else {
          error(CaseExpected);
          return nullptr;
        }
      }
      return switch_case(cond.release(), std::move(cases), default_case.release());
    }
    case Token::ID("break"): {
      read();
      read_semicolons();
      return flow_break();
    }
    case Token::ID("return"): {
      read();
      if (peek_eol() || peek_end()) return flow_return();
      auto e = expression();
      if (!e) return nullptr;
      read_semicolons();
      return flow_return(e);
    }
    case Token::ID("throw"): {
      read();
      if (peek_eol(MissingExpression) || peek_end(MissingExpression)) return nullptr;
      auto e = expression();
      if (!e) return nullptr;
      read_semicolons();
      return flow_throw(e);
    }
    case Token::ID("try"): {
      read();
      if (!peek(Token::ID("{"), TokenExpected)) return nullptr;
      auto stmt = std::unique_ptr<Stmt>(statement());
      if (!stmt) return nullptr;
      std::unique_ptr<Expr> catch_clause;
      std::unique_ptr<Stmt> finally_clause;
      if (read(Token::ID("catch"))) {
        std::string name;
        if (read(Token::ID("("))) {
          auto name = read_identifier(MissingIdentifier);
          if (name.empty()) return nullptr;
          if (!read(Token::ID(")"), TokenExpected)) return nullptr;
        }
        if (!peek(Token::ID("{"), TokenExpected)) return nullptr;
        auto stmt = statement();
        if (!stmt) return nullptr;
        catch_clause = std::unique_ptr<Expr>(
          function(name.empty() ? nullptr : identifier(name), stmt)
        );
      }
      if (read(Token::ID("finally"))) {
        if (!peek(Token::ID("{"), TokenExpected)) return nullptr;
        auto stmt = statement();
        if (!stmt) return nullptr;
        finally_clause = std::unique_ptr<Stmt>(stmt);
      }
      if (!catch_clause && !finally_clause) {
        error(MissingCatchFinally);
        return nullptr;
      }
      return try_catch(stmt.release(), catch_clause.release(), finally_clause.release());
    }
    default: {
      auto e = expression();
      if (!e) return nullptr;
      read(Token::ID(";"));
      return evaluate(e);
    }
  }
}

Expr* ScriptParser::expression(bool no_comma) {
  std::stack<Token> operators;
  std::stack<Location> locations;
  std::stack<std::unique_ptr<Expr>> operands;

  // Do at least once and repeat until stack is empty
  do {

    // When the last operator is parenthesis-like
    int last_operator = operators.empty() ? 0 : operators.top().id();
    if (last_operator == Token::ID("("  ) ||
        last_operator == Token::ID("["  ) ||
        last_operator == Token::ID("?.(") ||
        last_operator == Token::ID("?.["))
    {
      auto is_call = (last_operator == Token::ID("(") || last_operator == Token::ID("?.("));
      Token closing(is_call ? Token::ID(")") : Token::ID("]"));

      // Parse arguments
      std::vector<std::unique_ptr<Expr>> argv;
      for (;;) {
        auto t = peek();
        if (t == Token::eof) return error(UnexpectedEOF);
        if (t == Token::err) return error(UnknownToken);
        if (t == closing) break;
        auto e = expression(is_call);
        if (!e) return nullptr;
        argv.push_back(std::unique_ptr<Expr>(e));
        if (!is_call) break;
        read(Token::ID(","));
      }

      // Check closing parenthesis
      auto t = peek();
      if (t == Token::eof) return error(UnexpectedEOF);
      if (t == Token::err) return error(UnknownToken);
      if (t == closing) read();
      else return error(UnexpectedToken);

      // Make the operand
      operators.pop();
      auto l = locations.top(); locations.pop();
      auto o = operands.top().release(); operands.pop();
      auto is_new = (!operators.empty() && operators.top().id() == Token::ID("new"));
      if (is_call && is_new) {
        if (last_operator == Token::ID("?.(")) return error(InvalidOptionalChain);
        auto e = construct(o, std::move(argv));
        operators.pop(); // pop 'new'
        locations.pop();
        operands.pop(); // pop the empty operand
        operands.push(std::unique_ptr<Expr>(locate(e, l)));
      } else if (is_call) {
        auto e = (last_operator == Token::ID("?.(") ? opt_call(o, std::move(argv)) : call(o, std::move(argv)));
        operands.push(std::unique_ptr<Expr>(locate(e, l)));
      } else if (argv.size() != 1) {
        return error(UnexpectedToken);
      } else {
        auto k = argv[0].release();
        auto e = (last_operator == Token::ID("?.[") ? opt_prop(o, k) : prop(o, k));
        operands.push(std::unique_ptr<Expr>(locate(e, l)));
      }

    // When the last operator is dot-like
    } else if (
      last_operator == Token::ID(".") ||
      last_operator == Token::ID("?.")
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
        if (t == Token::eof) return error(UnexpectedEOF);
        if (t == Token::err) return error(UnknownToken);
        switch (t.id()) {
          case Token::ID("+"):
            read(l);
            operands.push(nullptr);
            operators.push(Token::ID("+x"));
            locations.push(l);
            continue;
          case Token::ID("-"):
            read(l);
            operands.push(nullptr);
            operators.push(Token::ID("-x"));
            locations.push(l);
            continue;
          case Token::ID("++"):
            read(l);
            operands.push(nullptr);
            operators.push(Token::ID("++x"));
            locations.push(l);
            continue;
          case Token::ID("--"):
            read(l);
            operands.push(nullptr);
            operators.push(Token::ID("--x"));
            locations.push(l);
            continue;
          case Token::ID("~"):
          case Token::ID("!"):
          case Token::ID("void"):
          case Token::ID("typeof"):
          case Token::ID("new"):
          case Token::ID("delete"):
            operands.push(nullptr);
            operators.push(read(l));
            locations.push(l);
            continue;
        }
        break;
      }

      // Trailing comma of a list
      auto t = peek();
      if (t.id() == Token::ID(")") &&
          operators.size() > 0 &&
          operators.top().id() == Token::ID(",")
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
      bool eol = peek_eol();
      auto t = peek();
      auto is_end = (
        (t == Token::eof) ||
        (t.id() == Token::ID(";")) ||
        (t.id() == Token::ID(")")) ||
        (t.id() == Token::ID("]")) ||
        (t.id() == Token::ID("}")) ||
        (t.id() == Token::ID(",") && no_comma) ||
        (t.id() == Token::ID(":") && (operators.empty() || operators.top().id() != Token::ID("?"))) ||
        (t.id() == Token::ID("case")) ||
        (t.id() == Token::ID("default"))
      );
      if (t == Token::err) return error(UnknownToken);
      if (!is_end && !t.is_operator()) {
        if (eol) {
          t = Token::eof;
          is_end = true;
        } else {
          return error(UnexpectedToken);
        }
      }

      // Some operators are invalid here, post operators are converted
      switch (t.id()) {
        case Token::ID("~"):
        case Token::ID("!"):
        case Token::ID("void"):
        case Token::ID("typeof"):
        case Token::ID("new"):
        case Token::ID("delete"):
          return error(UnexpectedToken);
        case Token::ID("++"):
          if (eol) {
            t = Token::eof;
            is_end = true;
          } else {
            t = Token(Token::ID("x++"));
          }
          break;
        case Token::ID("--"):
          if (eol) {
            t = Token::eof;
            is_end = true;
          } else {
            t = Token(Token::ID("x--"));
          }
          break;
      }

      // Reduce earlier operators
      while (!operators.empty() && (is_end || precedes(operators.top(), t))) {
        Expr *e = nullptr;
        auto b = operands.top().release(); operands.pop();
        auto a = operands.top().release(); operands.pop();
        auto o = operators.top(); operators.pop();
        auto l = locations.top(); locations.pop();
        if (t.id() == Token::ID("**")) {
          if (o.id() == Token::ID("~" ) ||
              o.id() == Token::ID("!" ) ||
              o.id() == Token::ID("+x") ||
              o.id() == Token::ID("-x")
          ) {
            return error(AmbiguousPrecedence);
          }
        }
        switch (o.id()) {
          case Token::ID("instanceof"): e = instance_of(a, b); break;
          case Token::ID("typeof"): e = type_of(b); break;
          case Token::ID("new"): e = construct(b); break;
          case Token::ID("delete"): e = del(b); break;
          case Token::ID("void"): e = discard(b); break;
          case Token::ID("+x"  ): e = pos(b); break;
          case Token::ID("-x"  ): e = neg(b); break;
          case Token::ID("~"   ): e = bit_not(b); break;
          case Token::ID("!"   ): e = bool_not(b); break;
          case Token::ID("+"   ): e = add(a, b); break;
          case Token::ID("-"   ): e = sub(a, b); break;
          case Token::ID("*"   ): e = mul(a, b); break;
          case Token::ID("**"  ): e = pow(a, b); break;
          case Token::ID("/"   ): e = div(a, b); break;
          case Token::ID("%"   ): e = rem(a, b); break;
          case Token::ID("<<"  ): e = shl(a, b); break;
          case Token::ID(">>"  ): e = shr(a, b); break;
          case Token::ID(">>>" ): e = usr(a, b); break;
          case Token::ID("&"   ): e = bit_and(a, b); break;
          case Token::ID("|"   ): e = bit_or(a, b); break;
          case Token::ID("^"   ): e = bit_xor(a, b); break;
          case Token::ID("&&"  ): e = bool_and(a, b); break;
          case Token::ID("||"  ): e = bool_or(a, b); break;
          case Token::ID("??"  ): e = null_or(a, b); break;
          case Token::ID("=="  ): e = eql(a, b); break;
          case Token::ID("!="  ): e = neq(a, b); break;
          case Token::ID("===" ): e = same(a, b); break;
          case Token::ID("!==" ): e = diff(a, b); break;
          case Token::ID(">"   ): e = gt(a, b); break;
          case Token::ID(">="  ): e = ge(a, b); break;
          case Token::ID("<"   ): e = lt(a, b); break;
          case Token::ID("<="  ): e = le(a, b); break;
          case Token::ID("in"  ): e = in(a, b); break;
          case Token::ID("="   ): e = assign(a, b); break;
          case Token::ID("+="  ): e = add_assign(a, b); break;
          case Token::ID("-="  ): e = sub_assign(a, b); break;
          case Token::ID("*="  ): e = mul_assign(a, b); break;
          case Token::ID("/="  ): e = div_assign(a, b); break;
          case Token::ID("%="  ): e = rem_assign(a, b); break;
          case Token::ID("**=" ): e = pow_assign(a, b); break;
          case Token::ID("<<=" ): e = shl_assign(a, b); break;
          case Token::ID(">>=" ): e = shr_assign(a, b); break;
          case Token::ID(">>>="): e = usr_assign(a, b); break;
          case Token::ID("&="  ): e = bit_and_assign(a, b); break;
          case Token::ID("|="  ): e = bit_or_assign(a, b); break;
          case Token::ID("^="  ): e = bit_xor_assign(a, b); break;
          case Token::ID("&&=" ): e = bool_and_assign(a, b); break;
          case Token::ID("||=" ): e = bool_or_assign(a, b); break;
          case Token::ID("?\?="): e = null_or_assign(a, b); break;
          case Token::ID("x++" ): e = post_inc(a); break;
          case Token::ID("x--" ): e = post_dec(a); break;
          case Token::ID("++x" ): e = pre_inc(b); break;
          case Token::ID("--x" ): e = pre_dec(b); break;
          case Token::ID(","   ): e = compound(a, b); break;
          case Token::ID("."   ):
          case Token::ID("?."  ): {
            auto s = identifier_to_string(b);
            if (!s) {
              delete a;
              delete b;
              return error(UnexpectedToken);
            } else {
              delete b;
            }
            e = (o.id() == Token::ID("?.") ? opt_prop(a, s) : prop(a, s));
            break;
          }
          case Token::ID(":"): {
            if (operators.empty() || operators.top().id() != Token::ID("?")) {
              return error(UnexpectedToken);
            }
            auto c = operands.top().release();
            operands.pop();
            operators.pop();
            locations.pop();
            e = select(c, a, b);
            break;
          }
          case Token::ID("?"): {
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
      if (t.id() == Token::ID("x++") || t.id() == Token::ID("x--")) {
        operands.push(nullptr);
        continue;
      }
      break;
    }
  } while (!operators.empty());

  return operands.top().release();
}

Expr* ScriptParser::operand() {
  auto t = peek();
  if (t.id() == Token::ID("(")) {
    Location l;
    read(l);
    if (read(Token::ID(")"))) {
      if (auto f = arrow_function(l, nullptr)) return f;
      if (has_error()) return nullptr;
      return error(TokenExpected, Token::ID("=>")); // Must be an arrow function
    }
    auto e = expression();
    if (!e) return nullptr;
    auto t = peek();
    if (t == Token::eof) return error(UnexpectedEOF);
    if (t == Token::err) return error(UnknownToken);
    if (t.id() != Token::ID(")")) return error(UnexpectedToken);
    Location loc;
    read(loc);

    // Could it be an arrow function?
    if (auto f = arrow_function(l, e)) return f;
    if (has_error()) {
      delete e;
      return nullptr;
    }

    if (e->is_comma_ended()) {
      m_location = loc;
      return error(IncompleteExpression);
    }

    return e;
  }

  if (t.id() == Token::ID("`")) {
    Location l;
    read(l);
    std::list<std::unique_ptr<Expr>> list;
    m_tokenizer.set_template_mode(true);
    for (;;) {
      auto t = peek();
      if (t == Token::eof) return error(UnexpectedEOF);
      if (t == Token::err) return error(UnknownToken);
      if (t.id() == Token::ID("`")) {
        read();
        break;
      } else if (t.id() == Token::ID("${")) {
        read();
        m_tokenizer.set_template_mode(false);
        auto e = expression();
        if (!e) return nullptr;
        list.push_back(std::unique_ptr<Expr>(e));
        auto t = peek();
        if (t == Token::eof) return error(UnexpectedEOF);
        if (t == Token::err) return error(UnknownToken);
        if (t.id() != Token::ID("}")) return error(UnexpectedToken);
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

  if (t.id() == Token::ID("function")) {
    Location l;
    read(l);
    read_identifier();
    return block_function(l);
  }

  switch (t.id()) {
    case Token::ID("undefined"): read(); return locate(undefined());
    case Token::ID("null"): read(); return locate(null());
    case Token::ID("false"): read(); return locate(boolean(false));
    case Token::ID("true"): read(); return locate(boolean(true));
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
      if (auto f = arrow_function(l, e)) return f;
      if (has_error()) {
        delete e;
        return nullptr;
      }
      return e;
    }
  }

  if (t.id() == Token::ID("{")) {
    Location l;
    read(l);
    std::list<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> list;
    for (;;) {
      auto t = peek();
      if (t.id() == Token::ID("}")) {
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
        char str[100];
        auto len = Number::to_string(str, sizeof(str), t.n());
        k = locate(string(std::string(str, len)));
      } else if (t.id() == Token::ID("[")) {
        read();
        k = expression();
        if (!k) return nullptr;
        if (!read(Token::ID("]"), TokenExpected)) return nullptr;
      } else if (t.id() == Token::ID("...")) {
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
        if (!key.empty() && (t.id() == Token::ID(",") || t.id() == Token::ID("}"))) {
          v = locate(identifier(key), k);
        } else if (t.id() == Token::ID(":")) {
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
      if (t.id() == Token::ID(",")) read();
    }
    return locate(object(list), l);
  }

  if (t.id() == Token::ID("[")) {
    Location l;
    read(l);
    std::list<std::unique_ptr<Expr>> list;
    for (;;) {
      auto t = peek();
      if (t.id() == Token::ID("]")) {
        read();
        break;
      }
      if (t.id() == Token::ID("...")) {
        read();
        auto v = expression(true);
        if (!v) return nullptr;
        list.push_back(std::unique_ptr<Expr>(expand(v)));
      } else {
        auto v = expression(true);
        if (!v) return nullptr;
        list.push_back(std::unique_ptr<Expr>(v));
      }
      read(Token::ID(","));
    }
    return locate(array(std::move(list)), l);
  }

  return error(UnexpectedToken);
}

Expr* ScriptParser::block_function(Location &loc) {
  std::unique_ptr<Expr> arguments;
  if (!read(Token::ID("("), TokenExpected)) return nullptr;
  if (!read(Token::ID(")"))) {
    arguments = std::unique_ptr<Expr>(expression());
    if (!read(Token::ID(")"), TokenExpected)) return nullptr;
    if (!arguments->is_argument() && !arguments->is_argument_list()) {
      return error(InvalidArgumentList);
    }
  }
  if (!read(Token::ID("{"), TokenExpected)) return nullptr;
  auto body = std::unique_ptr<Stmt>(statement_block());
  if (!body) return nullptr;
  if (!read(Token::ID("}"), TokenExpected)) return nullptr;
  return locate(function(arguments.release(), body.release()), loc);
}

Expr* ScriptParser::arrow_function(Location &loc, Expr *arguments) {
  bool eol = peek_eol();
  if (!read(Token::ID("=>"))) return nullptr;
  if (eol) return error(UnexpectedEOL);
  if (arguments) {
    if (!arguments->is_argument() && !arguments->is_argument_list()) {
      return error(InvalidArgumentList);
    }
  }
  if (read(Token::ID("{"))) {
    auto s = std::unique_ptr<Stmt>(statement_block());
    if (!s) return nullptr;
    if (!read(Token::ID("}"), TokenExpected)) return nullptr;
    return locate(function(arguments, s.release()), loc);
  } else {
    auto f = expression(true);
    if (!f) return nullptr;
    return locate(function(arguments, f), loc);
  }
}

//
// Parser
//

auto Parser::parse(
  const Source *source,
  std::string &error,
  int &error_line,
  int &error_column) -> Stmt*
{
  Token::clear();
  ScriptParser parser(source);
  return parser.parse(error, error_line, error_column);
}


auto Parser::parse_expr(
  const Source *source,
  std::string &error,
  int &error_line,
  int &error_column) -> Expr*
{
  Token::clear();
  ScriptParser parser(source);
  return parser.parse_expr(error, error_line, error_column);
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
