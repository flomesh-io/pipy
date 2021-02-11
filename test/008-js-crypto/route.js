const ROUTES = {
  '/sign' : 'sign',
  '/veri' : 'verify',
  '/enc'  : 'encrypt',
  '/dec'  : 'decrypt',
};

export default (output, context) => (
  input => {
    if (input instanceof Event &&
        input.type === 'messagestart'
    ) {
      const path = context.get('a.path');
      const target = ROUTES[path];
      if (target === undefined) {
        context.set('target', '404');
        context.set('a.status_code', '404');
        context.set('a.status', 'Not Found');
      } else {
        context.set('target', target);
        context.set('a.status_code', '200');
        context.set('a.status', 'OK');
      }
    }

    output(input);
  }
);

