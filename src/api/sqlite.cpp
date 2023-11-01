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

#include "sqlite.hpp"

namespace pipy {
namespace sqlite {

//
// Database
//

Database::Database(pjs::Str *filename) {
  if (sqlite3_open(filename->c_str(), &m_db) != SQLITE_OK) {
    std::string msg("Unable to open Sqlite database at ");
    msg += filename->str();
    msg += ": ";
    msg += sqlite3_errmsg(m_db);
    throw std::runtime_error(msg);
  }
}

Database::~Database() {
  sqlite3_close(m_db);
}

auto Database::sql(pjs::Str *sql) -> Statement* {
  return nullptr;
}

//
// Statement
//

auto Statement::reset() -> Statement* {
  sqlite3_reset(m_stmt);
  return this;
}

auto Statement::bind(int i, const pjs::Value &v) -> Statement* {
  return this;
}

auto Statement::step() -> Result {
  switch (sqlite3_step(m_stmt)) {
    case SQLITE_ROW: return Result::ROW;
    case SQLITE_DONE: return Result::DONE;
    case SQLITE_BUSY: return Result::BUSY;
    case SQLITE_ERROR: return Result::ERROR;
    case SQLITE_MISUSE: return Result::MISUSE;
    default: return Result::ERROR;
  }
}

void Statement::column(int i, pjs::Value &v) {
}

//
// Sqlite
//

auto Sqlite::database(pjs::Str *filename) -> Database* {
  return Database::make(filename);
}

auto Sqlite::exec(const pjs::Str *sql) -> pjs::Array* {
  return nullptr;
}

void Sqlite::operator()(pjs::Context &ctx, pjs::Object *obj, pjs::Value &ret) {
  pjs::Str *filename;
  if (!ctx.arguments(1, &filename)) return;
  try {
    ret.set(database(filename));
  } catch (std::runtime_error &err) {
    ctx.error(err);
  }
}

} // namespace sqlite
} // namespace pipy

namespace pjs {

using namespace pipy::sqlite;

template<> void ClassDef<Sqlite>::init() {
  super<Function>();
  ctor();

  method("exec", [](Context &ctx, Object*, Value &ret) {
    Str *sql;
    if (!ctx.arguments(1, &sql)) return;
    ret.set(Sqlite::exec(sql));
  });
}

template<> void ClassDef<Database>::init() {
  method("sql", [](Context &ctx, Object *obj, Value &ret) {
    pjs::Str *sql;
    if (!ctx.arguments(1, &sql)) return;
    ret.set(static_cast<Database*>(obj)->sql(sql));
  });
}

template<> void ClassDef<Statement>::init() {
  method("reset", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(static_cast<Statement*>(obj)->reset());
  });

  method("bind", [](Context &ctx, Object *obj, Value &ret) {
    int i;
    Value v;
    if (!ctx.arguments(1, &i, &v)) return;
    ret.set(static_cast<Statement*>(obj)->bind(i, v));
  });

  method("step", [](Context &ctx, Object *obj, Value &ret) {
  });

  method("column", [](Context &ctx, Object *obj, Value &ret) {
    int i;
    if (!ctx.arguments(1, &i)) return;
    try {
      static_cast<Statement*>(obj)->column(i, ret);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

} // namespace pjs
