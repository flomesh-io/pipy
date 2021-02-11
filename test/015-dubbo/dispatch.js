import ROUTES from './routes.js';

export default (output, context) => (
  input => {

    // Find route on every message start
    if (input instanceof Event &&
        input.type === 'messagestart'
    ) {
      const path = context.get('http.path');
      const target = ROUTES[path];
      context.set('http.status', target ? '200' : '404');
      context.set('pipeline', target);
    }

    // Pass down everything
    output(input);
  }
);
