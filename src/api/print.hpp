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

#ifndef API_PRINT_H
#define API_PRINT_H

#include "pjs/pjs.hpp"

namespace pipy {

void print(int argc, const pjs::Value argv[]);
void println(int argc, const pjs::Value argv[]);

//
// PrintFunction
//

class PrintFunction : public pjs::FunctionTemplate<PrintFunction> {
public:
  void operator()(pjs::Context &ctx, pjs::Object *obj, pjs::Value &ret) {
    print(ctx.argc(), ctx.argv());
  }
};

//
// PrintlnFunction
//

class PrintlnFunction : public pjs::FunctionTemplate<PrintlnFunction> {
public:
  void operator()(pjs::Context &ctx, pjs::Object *obj, pjs::Value &ret) {
    println(ctx.argc(), ctx.argv());
  }
};

} // namespace pipy

#endif // API_PRINT_H
