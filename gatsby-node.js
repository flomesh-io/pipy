/**
 * Implement Gatsby's Node APIs in this file.
 *
 * See: https://www.gatsbyjs.com/docs/node-apis/
 */

const MonacoWebpackPlugin = require('monaco-editor-webpack-plugin');

exports.onCreateWebpackConfig = ({
  actions,
}) => {
  actions.setWebpackConfig({
    plugins: [
      new MonacoWebpackPlugin(),
    ],
  });
}

const LANGS = ['en', 'zh'];
const HOME = 'overview';

exports.onCreateNode = ({ node, actions }) => {
  const { createNodeField } = actions;

  let path = node.fileAbsolutePath;
  if (node.internal.type === 'Mdx' && path.endsWith('.mdx')) {
    path = path.substring(0, path.length - 4);
    let i = path.lastIndexOf('/docs/');
    if (i >= 0) {
      path = path.substring(i + 6);

      let lang = 'en';
      i = path.lastIndexOf('.');
      if (i >= 0) {
        const ext = path.substring(i + 1);
        if (LANGS.includes(ext)) {
          lang = ext;
          path = path.substring(0, i);
        }
      }

      createNodeField({
        node,
        name: 'path',
        value: path,
      });

      createNodeField({
        node,
        name: 'lang',
        value: lang,
      });
    }
  }
}

exports.createPages = async ({ graphql, actions }) => {
  const { createPage } = actions;
  const result = await graphql(`
    query {
      allMdx {
        nodes {
          id
          fields {
            path
            lang
          }
          frontmatter {
            api
          }
        }
      }
    }
  `);

  const componentDocPage = require.resolve('./gui/src/components/doc-page.js');
  const componentDocRedirect = require.resolve('./gui/src/components/doc-redirect.js');

  const pages = {};

  result.data.allMdx.nodes.forEach(node => {
    const { path, lang } = node.fields;
    if (path && lang) {
      const group = pages[path] || (pages[path] = {});
      group[lang] = node;
    }
  });

  Object.keys(pages).forEach(path => {
    const group = pages[path];

    for (const lang of LANGS) {
      const node = group[lang] || group['en'];
      if (node) {
        const { id, frontmatter } = node;

        const { api } = frontmatter;
        let kind, name, parent;
        if (api) {
          if (api.endsWith('()')) {
            kind = 'function';
            name = api.substring(0, api.length - 2);
          } else {
            kind = 'class';
            name = api;
          }
          const i = api.indexOf('.');
          if (i >= 0) {
            parent = name.substring(0, i);
            name = name.substring(i + 1);
          }
        }  

        createPage({
          path: `/docs/${lang}/${path}`,
          component: componentDocPage,
          context: { id, kind, name, parent },
        });

        if (path == HOME) {
          createPage({
            path: `/docs/${lang}`,
            component: componentDocPage,
            context: { id, kind, name, parent },
          });
        }
      }
    }

    createPage({
      path: `/docs/${path}`,
      component: componentDocRedirect,
      contenxt: {},
    });

    if (path == HOME) {
      createPage({
        path: `/docs`,
        component: componentDocRedirect,
        contenxt: {},
      });
    }
  });
}
