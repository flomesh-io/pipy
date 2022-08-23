/**
 * Implement Gatsby's Node APIs in this file.
 *
 * See: https://www.gatsbyjs.com/docs/node-apis/
 */

const fs = require('fs');
const MonacoWebpackPlugin = require('monaco-editor-webpack-plugin');
const { Application, TypeDocReader, TSConfigReader } = require("typedoc");

exports.onCreateWebpackConfig = ({
  actions,
}) => {
  actions.setWebpackConfig({
    plugins: [
      new MonacoWebpackPlugin(),
    ],
  });
}

exports.sourceNodes = ({ actions, createNodeId, createContentDigest }) => {
  const { createNode } = actions;

  const dtsFilenames = fs
    .readdirSync('docs/dts')
    .filter(s => s.endsWith('.d.ts'))
    .map(s => `docs/dts/${s}`);

  console.log('===== DTS files =====');

  for (const filename of dtsFilenames) {
    console.log(filename);
    const content = fs.readFileSync(filename, 'utf-8');
    createNode({
      id: createNodeId(filename),
      filename,
      parent: null,
      children: [],
      internal: {
        type: 'DTS',
        mediaType: 'text/plain',
        content,
        contentDigest: createContentDigest(content),
      },
    });
  }

  const app = new Application();
  app.options.addReader(new TypeDocReader());
  app.options.addReader(new TSConfigReader());

  app.bootstrap({
    tsconfig: `${__dirname}/docs/dts/tsconfig.json`,
  });

  app.options.setValue('entryPoints', dtsFilenames);

  const reflection = app.convert();
  const tree = app.serializer.toObject(reflection);
  const nodes = [];

  const visitNode = (node, parent) => {
    switch (node.kindString) {
      case 'Variable':
      case 'Function':
      case 'Namespace':
      case 'Interface':
      case 'Method':
      case 'Property':
        nodes.push(node);
        break;
    }
    if (parent && parent.kindString === 'Namespace') node.namespace = parent.name;
    if (node.children) {
      for (const child of node.children) {
        visitNode(child, node);
      }
    }
  }

  visitNode(tree, null);

  const findNode = (kind, name) => nodes.find(i => i.kindString === kind && i.name == name);
  const findClassFromConstructor = (name) => findNode('Interface', name.substring(0, name.length - 11));

  const namespaces = {};
  const classes = {};
  const functions = {};

  for (const node of nodes) {
    if (node.kindString === 'Interface' && !node.name.endsWith('Constructor') && !node.name.endsWith('Options')) {
      if (node.children && node.children.length > 0 && !node.extendedBy) {
        classes[node.name] = node;
      }
    }
  }

  for (const node of nodes) {
    if (node.kindString === 'Variable') {
      if (node.type.name.endsWith('Constructor')) {
        const classNode = findClassFromConstructor(node.type.name);
        classNode.constructorClass = findNode('Interface', node.type.name);
        classes[node.name] = classNode;
      } else {
        const namespaceNode = findNode('Interface', node.type.name);
        namespaces[node.name] = namespaceNode;
        delete classes[node.type.name];
      }
    } else if (node.kindString === 'Function') {
      let name = node.name;
      if (node.namespace) name = node.namespace + '.' + name;
      functions[name] = node;
    }
  }

  const addNode = (name, data) => {
    const content = JSON.stringify(data);
    createNode({
      id: createNodeId(name),
      filename: name,
      parent: null,
      children: [],
      internal: {
        type: 'TypeDoc',
        mediaType: 'application/json',
        content,
        contentDigest: createContentDigest(content),
      },
    });
  }

  const addClassMembers = (className, classNode) => {
    const children = classNode.children;
    if (children) {
      for (const child of classNode.children) {
        switch (child.kindString) {
          case 'Method':
            addNode(className + '.' + child.name + '()', child);
            break;
          case 'Property':
            addNode(className + '.' + child.name, child);
            break;
        }
      }
    }
    const constructor = classNode.constructorClass;
    if (constructor) {
      for (const child of constructor.children) {
        switch (child.kindString) {
          case 'Constructor':
            child.className = className;
            addNode(className + '.new()', child);
            break;
          case 'Method':
            addNode(className + '.' + child.name + '()', child);
            break;
        }
      }
    }
  }

  for (const name in namespaces) {
    const node = namespaces[name];
    for (const child of node.children) {
      if (child.kindString === 'Method') {
        addNode(name + '.' + child.name + '()', child);
      } else if (child.kindString === 'Property') {
        const type = child.type.name;
        if (type && type.endsWith('Constructor')) {
          const classNode = findClassFromConstructor(child.type.name);
          if (classNode) {
            const className = name + '.' + child.name;
            classNode.className = className;
            classNode.constructorClass = findNode('Interface', child.type.name);
            child.classNode = classNode;
            addClassMembers(className, classNode);
            addNode(className, classNode);
            delete classes[classNode.name];
          }
        }
      }
    }
    addNode(name, node);
  }

  for (const name in classes) {
    const node = classes[name];
    node.className = name;
    addClassMembers(name, node);
    addNode(name, node);
  }

  for (const name in functions) {
    const node = functions[name];
    addNode(name + '()', node);
  }

  console.log('===== TypeDoc =====');
  console.log('Namespaces');
  for (const name in namespaces) console.log(' ', name);
  console.log('Classes');
  for (const name in classes) console.log(' ', name);
  console.log('Functions');
  for (const name in functions) console.log(' ', name);
}

const LANGS = ['en', 'zh', 'jp'];
const HOME = 'intro/overview';

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
    const { path, lang } = node.fields || {};
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

        createPage({
          path: `/docs/${lang}/${path}`,
          component: componentDocPage,
          context: { id, api },
        });

        if (path == HOME) {
          createPage({
            path: `/docs/${lang}`,
            component: componentDocPage,
            context: { id, api },
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
