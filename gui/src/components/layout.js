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
      let ws, is_reconnecting = false;
      const node = document.createTextNode('');
      const loc = window.location;
      // const url = `ws://localhost:6060/api/v1/log//pipy_log`; // For development mode
      const url = `ws://${loc.host}/api/v1/log//pipy_log`;
      const connect = () => {
        const reconnect = () => {
          if (is_reconnecting) return;
          ws.close();
          setTimeout(() => { is_reconnecting = false; connect(); }, 5000);
          is_reconnecting = true;
        }
        ws = new WebSocket(url);
        ws.addEventListener('open', () => ws.send('watch\n'));
        ws.addEventListener('message', evt => node.appendData(evt.data));
        ws.addEventListener('close', reconnect);
        ws.addEventListener('error', reconnect);
      };
      connect();
      setLogTextNode(node);
      return () => ws.close();
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
