import * as React from "react"

const IndexPage = () => {
  const [ todoList, setTodoList ] = React.useState([]);

  React.useEffect(() => updateTodoList(), []);

  function updateTodoList() {
    fetch('/api/todos').then(
      res => res.json()
    ).then(
      res => setTodoList(res || [])
    );
  }

  function insertTodo() {
    const content = window.prompt('New to-do item');
    if (content) {
      fetch('/api/todos', { method: 'POST', body: content }).then(
        () => updateTodoList()
      );
    }
  }

  function updateTodo(id, old) {
    const content = window.prompt('Edit to-do item', old);
    if (content) {
      fetch(`/api/todos/${id}`, { method: 'PATCH', body: content }).then(
        () => updateTodoList()
      );
    }
  }

  function deleteTodo(id, content) {
    if (window.confirm('Delete this to-do item?\n' + content)) {
      fetch(`/api/todos/${id}`, { method: 'DELETE' }).then(
        () => updateTodoList()
      );
    }
  }

  function checkTodo(id, checked) {
    fetch(`/api/check-todo/${id}`, { method: 'PATCH', body: checked.toString() }).then(
      () => updateTodoList()
    );
  }

  return (
    <main>
      <h1>A Simple To-do List</h1>
      <div><button onClick={insertTodo}>Add To-do</button></div>
      <ul>
        {todoList.map(
          item => (
            <li>
              <div style={{
                display: 'flex',
                flexDirection: 'row',
                width: 500,
                margin: 10,
              }}>
                <input type="checkbox" checked={!!item.checked} onClick={() => checkTodo(item.id, !item.checked)}/>
                <div style={{ flexGrow: 1 }}>{item.content}</div>
                <button onClick={() => updateTodo(item.id, item.content)}>Edit</button>
                <button onClick={() => deleteTodo(item.id, item.content)}>Delete</button>
              </div>
            </li>
          )
        )}
      </ul>
    </main>
  )
}

export default IndexPage

export const Head = () => <title>A Simple To-do List</title>
