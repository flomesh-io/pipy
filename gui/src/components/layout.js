import React from 'react';

import { createTheme, ThemeProvider } from '@material-ui/core/styles';
import { QueryClient, QueryClientProvider } from 'react-query';

import CssBaseline from '@material-ui/core/CssBaseline';
import Console from './console';

import '@fontsource/titillium-web';

const theme = createTheme({
  palette: {
    type: 'dark',
    primary: {
      main: '#00adef',
    },
    text: {
      link: '#80adff',
      code: '#fff',
      codeBox: '#110',
    },
  },
  typography: {
    fontFamily: '"Titillium Web",Verdana,sans-serif',
  },
  overrides: {
    MuiButton: {
      root: {
        textTransform: 'none',
      },
    },
  },
  TAB_WIDTH: 60,
  TOOLBAR_HEIGHT: 50,
});

const queryClient = new QueryClient();

function Layout({ children }) {
  const [logTextNode, setLogTextNode] = React.useState(null);

  // Regularly fetch the log data
  React.useEffect(
    () => {
      let running = true;
      let size = 0;
      const node = document.createTextNode('');
      const poll = async () => {
        try {
          const res = await fetch('/api/v1/log', {
            headers: {
              'x-log-size': size,
            },
          });
          if (res.status === 200) {
            const more = parseInt(res.headers.get('x-log-size')) - size;
            if (more > 0) {
              const text = await res.text();
              const lines = text.split('\n').slice(-more-1);
              size += more;
              node.appendData(lines.join('\n'));
              const parentEl = node.parentElement;
              if (parentEl) parentEl.scrollIntoView(false);
              next(100);
              return;
            }
          }
          next(1000);
        } catch (err) {
          next(1000);
        }
      };
      const next = delay => {
        if (running) {
          window.setTimeout(poll, delay);
        }
      };
      setLogTextNode(node);
      next(1000);
      return () => running = false;
    },
    []
  );

  return (
    <Console.Context.Provider
      value={{
        textNode: logTextNode,
        clearLog: () => logTextNode && (logTextNode.data = ''),
      }}
    >
      <QueryClientProvider client={queryClient}>
        <ThemeProvider theme={theme}>
          <CssBaseline/>
          {children}
        </ThemeProvider>
      </QueryClientProvider>
    </Console.Context.Provider>
  );
}

export default Layout;