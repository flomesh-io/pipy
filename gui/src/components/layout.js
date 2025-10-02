import React from 'react';

import { createTheme, ThemeProvider } from '@material-ui/core/styles';
import { QueryClient, QueryClientProvider } from 'react-query';

import CssBaseline from '@material-ui/core/CssBaseline';

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
  return (
      <QueryClientProvider client={queryClient}>
        <ThemeProvider theme={theme}>
          <CssBaseline/>
          {children}
        </ThemeProvider>
      </QueryClientProvider>
  );
}

export default Layout;
