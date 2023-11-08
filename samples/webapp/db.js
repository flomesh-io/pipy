((
  db = sqlite('data.db')
) => (
  db.exec(
    `SELECT * FROM sqlite_schema WHERE type = 'table' AND name = 'todos'`
  ).length === 0 && (
    db.exec(`
      CREATE TABLE todos (
        id INTEGER PRIMARY KEY,
        content TEXT NOT NULL,
        checked INTEGER NOT NULL DEFAULT 0
      )
    `)
  ),

  {
    listTodos: () => (
      db.sql(
        `SELECT * FROM todos`
      )
      .exec()
    ),

    getTodo: (id) => (
      db.sql(
        `SELECT * FROM todos WHERE id = ${id}`
      )
      .exec()[0]
    ),

    insertTodo: (content) => (
      db.sql(
        `INSERT INTO todos (content) VALUES (?)`
      )
      .bind(1, content)
      .exec(),
      db.sql(
        `SELECT * FROM todos WHERE id = last_insert_rowid()`
      )
      .exec()[0]
    ),

    updateTodo: (id, content) => (
      db.sql(
        `UPDATE todos SET content = ? WHERE id = ?`
      )
      .bind(1, content)
      .bind(2, id)
      .exec(),
      db.sql(
        `SELECT * FROM todos WHERE id = ${id}`
      )
      .exec()[0]
    ),

    checkTodo: (id, checked) => (
      db.exec(
        `UPDATE todos SET checked = ${checked} WHERE id = ${id}`
      ),
      db.sql(
        `SELECT * FROM todos WHERE id = ${id}`
      )
      .exec()[0]
    ),

    deleteTodo: (id) => void (
      db.exec(
        `DELETE FROM todos WHERE id = ${id}`
      )
    ),
  }
))()
