/**
 * @type {import('gatsby').GatsbyConfig}
 */
module.exports = {
  siteMetadata: {
    siteUrl: `https://www.yourdomain.tld`,
  },
  proxy: [
    {
      prefix: `/api`,
      url: `http://localhost:8080`,
    },
  ],
  plugins: [],
}
