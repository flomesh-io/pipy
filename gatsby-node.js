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

  console.log('\n===== DTS files =====');

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

  const visitNode = (node) => {
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
    if (node.children) {
      for (const child of node.children) {
        visitNode(child, node);
      }
    }
  }

  // Find out all nodes in the TypeDoc tree
  visitNode(tree, null);

  // Categorize nodes from the TypeDoc tree
  const namespaces = {};
  const classes = {};
  const functions = {};

  const findNode = (kind, name) => nodes.find(i => i.kindString === kind && i.name == name);
  const findClassFromConstructor = (name) => findNode('Interface', name.substring(0, name.length - 11));

  for (const node of nodes) {
    if (node.kindString === 'Interface' && !node.name.endsWith('Constructor') && !node.name.endsWith('Options')) {
      if (node.children && node.children.length > 0 && !node.extendedBy) {
        classes[node.name] = node;
      }
    } else if (node.kindString === 'Function') {
      functions[node.id] = node;
    }
  }

  for (const node of nodes) {
    if (node.kindString === 'Variable') {
      if (node.type.type === 'reference' && node.type.name.endsWith('Constructor')) {
        const classNode = findClassFromConstructor(node.type.name);
        delete classes[classNode.name];
        classes[node.name] = classNode;
      } else {
        const namespaceNode = findNode('Interface', node.type.name);
        for (const child of namespaceNode.children) {
          if (child.kindString === 'Property') {
            if (child.type.type === 'reference' && child.type.name.endsWith('Constructor')) {
              const classNode = findClassFromConstructor(child.type.name);
              delete classes[classNode.name];
            }
          }
        }
        delete classes[node.type.name];
        namespaces[node.name] = namespaceNode;
      }
    } else if (node.kindString === 'Namespace') {
      for (const child of node.children) {
        if (child.kindString === 'Function') {
          delete functions[child.id];
        }
      }
      namespaces[node.name] = node;
    }
  }

  for (const id of Object.keys(functions)) {
    const f = functions[id];
    delete functions[id];
    functions[f.name] = f;
  }

  const makePropertyNode = (data, memberOf) => {
    const { name, comment } = data;
    return {
      kind: 'property',
      name,
      comment,
      memberOf,
    };
  }

  const makeFunctionNode = (data, memberOf) => {
    const { name, signatures } = data;
    const overloads = signatures.map(
      ({comment, parameters}) => ({
        comment,
        parameters: parameters?.map?.(({ name, comment, flags }) => ({ name, comment, flags }))
      })
    );
    const first = overloads[0];
    return {
      kind: 'function',
      name,
      comment: first?.comment,
      overloads,
      memberOf,
    }
  }

  const makeClassNode = (data, name) => {
    const constructorClass = findNode('Interface', data.name + 'Constructor');
    const constructors = [];
    const properties = [];
    const methods = [];
    const staticProperties = [];
    const staticMethods = [];
    if (data.children) {
      for (const child of data.children) {
        if (child.kindString === 'Property') {
          properties.push(makePropertyNode(child, name));
        } else if (child.kindString === 'Method') {
          methods.push(makeFunctionNode(child, name));
        }
      }
    }
    if (constructorClass) {
      for (const child of constructorClass.children) {
        if (child.kindString === 'Constructor') {
          constructors.push(makeFunctionNode(child, name));
        } if (child.kindString === 'Property') {
          staticProperties.push(makePropertyNode(child, name));
        } else if (child.kindString === 'Method') {
          staticMethods.push(makeFunctionNode(child, name));
        }
      }
    }
    return {
      name,
      kind: 'class',
      comment: data.comment,
      constructors,
      properties,
      methods,
      staticProperties,
      staticMethods,
    }
  }

  const makeNamespaceNode = (data, name) => {
    const variables = [];
    const functions = [];
    const classes = [];
    for (const child of data.children) {
      if (child.kindString === 'Method' || child.kindString === 'Function') {
        functions.push(makeFunctionNode(child, name));
      } else if (child.kindString === 'Property') {
        const type = child.type.name;
        if (type && type.endsWith('Constructor')) {
          const classData = findClassFromConstructor(child.type.name);
          classes.push({
            ...makeClassNode(classData, `${name}.${child.name}`),
            name: child.name,
            memberOf: name,
          });
        } else {
          variables.push(makePropertyNode(child, name));
        }
      }
    }
    return {
      name,
      kind: 'namespace',
      comment: data.comment,
      variables,
      functions,
      classes,
    }
  }

  // Create nodes
  const roots = [];
  for (const name in namespaces) roots.push(makeNamespaceNode(namespaces[name], name));
  for (const name in classes) roots.push(makeClassNode(classes[name], name));
  for (const name in functions) roots.push(makeFunctionNode(functions[name]));

  for (const name in namespaces) {
    if (name in functions) {
      const n = roots.find(i => i.kind === 'namespace' && i.name === name);
      const f = roots.find(i => i.kind === 'function' && i.name === name);
      if (f) {
        const { name, kind, comment, memberOf, ...rest } = n;
        Object.assign(f, rest);
      }
    }
  }

  const extractComments = ({ name, comment, overloads }) => {
    const ret = { name, comment };
    if (overloads) ret.overloads = overloads.map(({ comment, parameters }) => ({ comment, parameters }));
    return ret;
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

  const addClass = (name, data, memberOf) => {
    const {
      constructors,
      properties,
      methods,
      staticProperties,
      staticMethods,
      ...rest
    } = data;

    if (constructors.length > 0) {
      addNode(`${name}.new`, { memberOf: name, ...constructors[0] });
    }

    for (const child of properties      ) addNode(`${name}.${child.name}`, { memberOf: name, ...child });
    for (const child of methods         ) addNode(`${name}.${child.name}`, { memberOf: name, ...child });
    for (const child of staticProperties) addNode(`${name}.${child.name}`, { memberOf: name, ...child });
    for (const child of staticMethods   ) addNode(`${name}.${child.name}`, { memberOf: name, ...child });

    addNode(name, {
      memberOf,
      constructors    : constructors.map(extractComments),
      properties      : properties.map(extractComments),
      methods         : methods.map(extractComments),
      staticProperties: properties.map(extractComments),
      staticMethods   : methods.map(extractComments),
      ...rest
    });
  }

  const addNamespace = (name, data) => {
    const { variables, functions, classes } = data;
    if (name in functions) {
      for (const child in data.variables) addNode(`${name}.${child.name}`, child);
      for (const child in data.functions) addNode(`${name}.${child.name}`, child);
    } else {
      addNode(name, {
        variables: variables.map(extractComments),
        functions: functions.map(extractComments),
        classes  : classes.map(extractComments),
      });
    }
  }

  for (const root of roots) {
    switch (root.kind) {
      case 'namespace':
        const { variables, functions, classes } = root;
        for (const child of variables) addNode(`${root.name}.${child.name}`, { memberOf: root.name, ...child });
        for (const child of functions) addNode(`${root.name}.${child.name}`, { memberOf: root.name, ...child });
        for (const child of classes)  addClass(`${root.name}.${child.name}`, { memberOf: root.name, ...child }, root.name);
        addNamespace(root.name, root);
        break;
      case 'class':
        addClass(root.name, root);
        break;
      case 'function':
        addNode(root.name, root);
        break;
    }
  }

  console.log('\n===== Namespaces =====');
  for (const name in namespaces) console.log(name);
  console.log('\n===== Classes =====');
  for (const name in classes) console.log(name);
  console.log('\n===== Functions =====');
  for (const name in functions) console.log(name);
  console.log('');
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
