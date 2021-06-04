/**
 * Implement Gatsby's Browser APIs in this file.
 *
 * See: https://www.gatsbyjs.com/docs/browser-apis/
 */

const React = require('react');
const CssBaseline = require('@material-ui/core/CssBaseline').default;
const { createMuiTheme, ThemeProvider } = require('@material-ui/core/styles');

const theme = createMuiTheme({
  palette: {
    type: 'dark',
    primary: {
      main: '#ff0',
    },
  },
  TAB_WIDTH: 60,
  TOOLBAR_HEIGHT: 36,
});

exports.wrapPageElement = ({ element }) => {
  return (
    <ThemeProvider theme={theme}>
      <CssBaseline/>
      {element}
    </ThemeProvider>
  );
}