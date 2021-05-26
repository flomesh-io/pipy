module.exports = {
  siteMetadata: {
    title: `Pipy Console`,
  },
  proxy: [
    {
      prefix: `/api`,
      url: `http://localhost:6060`,
    },
  ],
  plugins: [
    `gatsby-plugin-material-ui`,
  ],
}