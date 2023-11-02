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
#include "data.hpp"

namespace pipy {
namespace sqlite {

thread_local static Data::Producer s_dp("SQLite");

static void free_blob_buffer(void *ptr) {
  std::free(ptr);
}

static void get_column_value(sqlite3_stmt *stmt, int col, pjs::Value &val) {
  if (0 <= col && col < sqlite3_column_count(stmt)) {
    switch (sqlite3_column_type(stmt, col)) {
      case SQLITE_INTEGER: {
        auto i = sqlite3_column_int64(stmt, col);
        if (i >> 32) {
          val.set(int64_t(i));
        } else {
          val.set(double(i));
        }
        break;
      }
      case SQLITE_FLOAT: {
        auto f = sqlite3_column_double(stmt, col);
        val.set(f);
        break;
      }
      case SQLITE_BLOB: {
        auto p = sqlite3_column_blob(stmt, col);
        auto n = sqlite3_column_bytes(stmt, col);
        val.set(s_dp.make(p, n));
        break;
      }
      case SQLITE_NULL: {
        val = pjs::Value::null;
        break;
      }
      case SQLITE_TEXT: {
        auto s = sqlite3_column_text(stmt, col);
        auto n = sqlite3_column_bytes(stmt, col);
        val.set(pjs::Str::make((char *)s, n));
        break;
      }
      default: val = pjs::Value::undefined;
    }
  } else {
    val = pjs::Value::undefined;
  }
}

static auto get_row_values(sqlite3_stmt *stmt) -> pjs::Object* {
  auto n = sqlite3_column_count(stmt);
  if (!n) return nullptr;
  auto o = pjs::Object::make();
  for (auto i = 0; i < n; i++) {
    pjs::Ref<pjs::Str> k(pjs::Str::make(sqlite3_column_name(stmt, i)));
    pjs::Value v;
    get_column_value(stmt, i, v);
    o->ht_set(k, v);
  }
  return o;
}

static int append_exec_row(void *rows, int n, char **values, char **names) {
  auto row = pjs::Object::make();
  for (int i = 0; i < n; i++) {
    pjs::Ref<pjs::Str> k(pjs::Str::make(names[i]));
    if (auto s = values[i]) {
      row->set(k, pjs::Str::make(s));
    } else {
      row->set(k, pjs::Value::null);
    }
  }
  static_cast<pjs::Array*>(rows)->push(row);
  return 0;
}

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

auto Database::exec(pjs::Str *sql) -> pjs::Array* {
  char *err = nullptr;
  auto rows = pjs::Array::make();
  sqlite3_exec(m_db, sql->c_str(), append_exec_row, rows, &err);
  if (err) {
    rows->retain();
    rows->release();
    std::string msg("SQLite exec() error: ");
    msg += err;
    throw std::runtime_error(msg);
  }
  return rows;
}

//
// Statement
//

auto Statement::reset() -> Statement* {
  sqlite3_reset(m_stmt);
  return this;
}

auto Statement::bind(int i, const pjs::Value &v) -> Statement* {
  switch (v.type()) {
    case pjs::Value::Type::Undefined: sqlite3_bind_null(m_stmt, i); break;
    case pjs::Value::Type::Boolean: sqlite3_bind_int(m_stmt, i, v.b()); break;
    case pjs::Value::Type::Number: sqlite3_bind_double(m_stmt, i, v.n()); break;
    case pjs::Value::Type::String: sqlite3_bind_text(m_stmt, i, v.s()->c_str(), v.s()->size(), SQLITE_TRANSIENT); break;
    case pjs::Value::Type::Object:
      if (auto o = v.o()) {
        if (o->is<pjs::Int>()) {
          auto l = o->as<pjs::Int>();
          sqlite3_bind_int64(m_stmt, i, l->value());
        } else if (o->is<Data>()) {
          auto d = o->as<Data>();
          auto buf = (uint8_t *)std::malloc(d->size());
          d->to_bytes(buf);
          sqlite3_bind_blob64(m_stmt, i, buf, d->size(), free_blob_buffer);
        } else {
          auto s = o->to_string();
          sqlite3_bind_text(m_stmt, i, s.c_str(), s.length(), SQLITE_TRANSIENT); break;
        }
      } else {
        sqlite3_bind_null(m_stmt, i);
      }
      break;
    default: sqlite3_bind_null(m_stmt, i); break;
  }
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
  get_column_value(m_stmt, i, v);
}

auto Statement::row() -> pjs::Object* {
  return get_row_values(m_stmt);
}

//
// Sqlite
//

auto Sqlite::database(pjs::Str *filename) -> Database* {
  return Database::make(filename);
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
}

template<> void ClassDef<Database>::init() {
  method("sql", [](Context &ctx, Object *obj, Value &ret) {
    pjs::Str *sql;
    if (!ctx.arguments(1, &sql)) return;
    ret.set(static_cast<Database*>(obj)->sql(sql));
  });

  method("exec", [](Context &ctx, Object *obj, Value &ret) {
    Str *sql;
    if (!ctx.arguments(1, &sql)) return;
    try {
      ret.set(static_cast<Database*>(obj)->exec(sql));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

template<> void EnumDef<Statement::Result>::init() {
  define(Statement::Result::ROW, "row");
  define(Statement::Result::DONE, "done");
  define(Statement::Result::BUSY, "busy");
  define(Statement::Result::ERROR, "error");
  define(Statement::Result::MISUSE, "misuse");
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
    auto result = static_cast<Statement*>(obj)->step();
    ret.set(EnumDef<Statement::Result>::name(result));
  });

  method("column", [](Context &ctx, Object *obj, Value &ret) {
    int i;
    if (!ctx.arguments(1, &i)) return;
    static_cast<Statement*>(obj)->column(i, ret);
  });

  method("row", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(static_cast<Statement*>(obj)->row());
  });
}

} // namespace pjs
