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

#ifndef SQLITE_HPP
#define SQLITE_HPP

#include "pjs/pjs.hpp"

#include <sqlite3.h>

namespace pipy {
namespace sqlite {

class Statement;

//
// Database
//

class Database : public pjs::ObjectTemplate<Database> {
public:
  auto sql(pjs::Str *sql) -> Statement*;

private:
  Database(pjs::Str *filename);
  ~Database();

  sqlite3* m_db;

  friend class pjs::ObjectTemplate<Database>;
};

//
// Statement
//

class Statement : public pjs::ObjectTemplate<Statement> {
public:

  //
  // Statement::Result
  //

  enum class Result {
    ROW,
    DONE,
    BUSY,
    ERROR,
    MISUSE,
  };

  auto reset() -> Statement*;
  auto bind(int i, const pjs::Value &v) -> Statement*;
  auto step() -> Result;
  void column(int i, pjs::Value &v);

private:
  Statement(sqlite3_stmt *stmt) : m_stmt(stmt) {}
  ~Statement() { sqlite3_finalize(m_stmt); }

  sqlite3_stmt* m_stmt;

  friend class pjs::ObjectTemplate<Statement>;
};

//
// Sqlite
//

class Sqlite : public pjs::FunctionTemplate<Sqlite> {
public:
  static auto database(pjs::Str *filename) -> Database*;
  static auto exec(const pjs::Str *sql) -> pjs::Array*;

  void operator()(pjs::Context &ctx, pjs::Object *obj, pjs::Value &ret);
};

} // namespace sqlite
} // namespace pipy

#endif // SQLITE_HPP
