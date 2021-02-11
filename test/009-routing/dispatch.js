import ROUTES from './routes.js';

export default (output, context) => (
  input => {

    // Find route on every message start
    if (input instanceof Event &&
        input.type === 'messagestart'
    ) {
      const path = context.get('a.path');
      const target = ROUTES[path];
      if (target === undefined) {
        context.set('pipeline', '404');
      } else {
        context.set('pipeline', 'up');
        context.set('target', target);
      }
    }

    // Pass down everything
    output(input);
  }
);
