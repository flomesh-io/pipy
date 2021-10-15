const React = require('react');
const Layout = require('./gui/src/components/layout').default;

exports.wrapPageElement = ({ element }) => {
  return <Layout>{element}</Layout>;
}
