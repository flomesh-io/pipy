export default function() {
  var queue = []
  var consumers = []

  function enqueue(req) {
    if (consumers.length > 0) {
      consumers.shift()(req)
    } else {
      queue.push(req)
    }
  }

  function dequeue() {
    if (queue.length > 0) {
      return queue.shift()
    } else {
      return new Promise(r => consumers.push(r))
    }
  }

  return { enqueue, dequeue }
}
