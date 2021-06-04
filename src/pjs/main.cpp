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

#include "pjs.hpp"

#include <iostream>

using namespace pjs;

class Console;

//
// Global
//

class Global : public ObjectTemplate<Global, Object> {
};

template<> void ClassDef<Global>::init() {
  ctor();
  variable("console", class_of<Console>());
}

//
// Console
//

class Console : public ObjectTemplate<Console, Object> {
};

template<> void ClassDef<Console>::init() {
  ctor();
  method("log", [](Context &ctx, Object*, Value &result) {
    for (int i = 0; i < ctx.argc(); i++) {
      if (i > 0) std::cout << ' ';
      auto str = ctx.arg(i).to_string();
      std::cout << str->c_str();
      str->release();
    }
    std::cout << std::endl;
    result = Value::undefined;
  });
}

//
// Tokenizer test
//

static void test_tokenizer(const char *script) {
  std::cout << "================" << std::endl;
  std::cout << script << std::endl;
  std::cout << "vvvvvvvvvvvvvvvv" << std::endl;
  auto tokens = Parser::tokenize(script);
  for (const auto &token : tokens) {
    std::cout << '[' << token << "] ";
  }
  std::cout << std::endl;
};

//
// Parser test
//

static void test_parser(const char *script) {
  std::cout << "================" << std::endl;
  std::cout << script << std::endl;
  std::cout << "vvvvvvvvvvvvvvvv" << std::endl;
  std::string error;
  int error_line, error_column;
  auto expr = Parser::parse(script, error, error_line, error_column);
  if (expr) {
    expr->dump(std::cout, "");
    delete expr;
  } else {
    std::cerr << "ERROR at line " << error_line << " column " << error_column << ": " << error << std::endl;
  }
}

//
// Execution test
//

static void test_eval(Context &ctx, const char *script) {
  std::cout << "================" << std::endl;
  std::cout << script << std::endl;
  std::cout << "vvvvvvvvvvvvvvvv" << std::endl;
  std::string error;
  int error_line, error_column;
  auto expr = Parser::parse(script, error, error_line, error_column);
  if (!expr) {
    std::cerr << "Syntax error at line " << error_line << " column " << error_column << ": " << error << std::endl;
    delete expr;
    return;
  }
  expr->resolve(ctx, 0);
  if (!ctx.error().message.empty()) {
    std::cerr << "Resolve error: " << ctx.error().message << std::endl;
    delete expr;
    return;
  }
  Value result;
  auto success = expr->eval(ctx, result);
  if (!success) {
    const auto &err = ctx.error();
    std::cerr << "Evaluation error: " << err.message << std::endl;
    for (const auto &l : err.backtrace) {
      std::string str("In ");
      str += l.name;
      if (l.line && l.column) {
        char s[100];
        std::sprintf(s, " at line %d column %d", l.line, l.column);
        str += s;
      }
      std::cerr << "    " << str << std::endl;
    }
    delete expr;
    return;
  }
  std::cout << "Result: " << result.to_string()->str() << std::endl;
}

//
// main
//

int main() {
  auto g = Global::make();

  Context ctx(g);

  test_tokenizer("undefined/null/true/false void new delete deleted intypeof in typeof instanceoff.instanceof ");
  test_tokenizer("(0+1)-[2]*{3}/4%5**6&7|8^9~a!b?c:d&&你||好??世界");
  test_tokenizer("+++++-----*****======>>>>>!!000\"\"??.....26?.()?.[]xyz");
  test_tokenizer("+=-=*=**=/=%=<<=>>=>>>=&=|=^=&&=||=\?\?=><>=<====!====!=");

  test_parser("x,8,'hello'+\" \"+'world',+'你好, world',1+~!2*3**p++>=5+--a?.b**6,(1+2)*(3,2),1");
  test_parser("f(),y=f(x),y=o.m(1,2+3,x+y,).n(null,'',true),new c.d,new i().foo(),delete a.b.c");
  test_parser("obj.foo()+-a**b**c");
  test_parser("obj.foo()+(-a)**b**c");
  test_parser("obj.foo()+-(a**b**c)");
  test_parser("()=>100,(x,y)=>(x+=y,x*y),()=>a?b:c");
  test_parser("{a:100,[b]:200,c,'d':300,...e},[1,'a',b,...c,]");

  test_eval(ctx, "console.log('hello', 'world')");
  test_eval(ctx, "((x, y) => x + y)(1, 2)");
  test_eval(ctx, "((x, cb) => cb(x))(1, x => x + 2)");
  test_eval(ctx, "(({x, y: [a, b]}) => x + a + b)({x: 1, y: [2, 3]})");

  return 0;
}