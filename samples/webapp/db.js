var db = sqlite('data.db')

if (db.exec(
  `SELECT * FROM sqlite_schema WHERE type = 'table' AND name = 'todos'`
).length === 0) {
  db.exec(`
    CREATE TABLE todos (
      id INTEGER PRIMARY KEY,
      content TEXT NOT NULL,
      checked INTEGER NOT NULL DEFAULT 0
    )
  `)
}

export function listTodos() {
  return db.sql(`SELECT * FROM todos`).exec()
}

export function getTodo(id) {
  return db.sql(`SELECT * FROM todos WHERE id = ${id}`).exec()[0]
}

export function insertTodo(content) {
  db.sql(`INSERT INTO todos (content) VALUES (?)`)
    .bind(1, content)
    .exec()
  return (
    db.sql(`SELECT * FROM todos WHERE id = last_insert_rowid()`)
      .exec()[0]
  )
}

export function updateTodo(id, content) {
  db.sql(`UPDATE todos SET content = ? WHERE id = ?`)
    .bind(1, content)
    .bind(2, id)
    .exec()
  return (
    db.sql(`SELECT * FROM todos WHERE id = ${id}`)
      .exec()[0]
  )
}

export function checkTodo(id, checked) {
  db.exec(`UPDATE todos SET checked = ${checked} WHERE id = ${id}`)
  return db.sql(`SELECT * FROM todos WHERE id = ${id}`).exec()[0]
}

export function deleteTodo(id) {
  return db.exec(`DELETE FROM todos WHERE id = ${id}`)
}
