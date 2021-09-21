const React = require('react');
const Layout = require('./src/components/layout').default;

exports.wrapPageElement = ({ element }) => {
  return <Layout>{element}</Layout>;
}