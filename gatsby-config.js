module.exports = {
  siteMetadata: {
    title: `Pipy`,
  },
  proxy: [
    {
      prefix: `/api`,
      url: `http://localhost:6060`,
    },
  ],
  plugins: [
    `gatsby-plugin-material-ui`,
    {
      resolve: `gatsby-plugin-mdx`,
      options: {
        defaultLayouts: {
          default: require.resolve('./gui/src/components/doc-page.js'),
        },
        remarkPlugins: [require('remark-slug')],
        shouldBlockNodeFromTransformation: node => {
          if (node.internal.type !== 'File' || node.ext !== '.mdx') return true;
          return false;
        },
      },
    },
    {
      resolve: `gatsby-source-filesystem`,
      options: {
        name: `doc`,
        path: `${__dirname}/docs/`,
      },
    },
    {
      resolve: `gatsby-plugin-page-creator`,
      options: {
        path: `${__dirname}/gui/src/pages/`,
      },
    },
    `gatsby-transformer-documentationjs`,
  ],
}
