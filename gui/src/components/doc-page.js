import React from 'react';
import Slugger from 'github-slugger';

import { graphql, navigate, Link } from 'gatsby';
import { useLocation } from '@reach/router';
import { MDXProvider } from '@mdx-js/react';
import { MDXRenderer } from 'gatsby-plugin-mdx';

import Button from '@material-ui/core/Button';
import Collapse from '@material-ui/core/Collapse';
import IconButton from '@material-ui/core/IconButton';
import Highlight, { defaultProps } from 'prism-react-renderer'
import List from '@material-ui/core/List';
import ListItem from '@material-ui/core/ListItem';
import ListItemText from '@material-ui/core/ListItemText';
import Popover from '@material-ui/core/Popover';
import Typography from '@material-ui/core/Typography';

import { makeStyles } from '@material-ui/core/styles';

import ExpandLessIcon from '@material-ui/icons/ExpandLess';
import ExpandMoreIcon from '@material-ui/icons/ExpandMore';
import ExternalLinkIcon from '@material-ui/icons/OpenInNewSharp';
import LangIcon from '@material-ui/icons/PublicSharp';
import TipIcon from '@material-ui/icons/EmojiObjectsSharp';

import PipyLogo from '../images/pipy.svg';
import HighlightTheme from 'prism-react-renderer/themes/vsDark';

const FONT_TITLE = '"Titillium Web", sans-serif';
const FONT_TOC = '"Titillium Web", verdana, sans-serif';
const FONT_TEXT = '"Titillium Web", graphik, verdana, arial, sans-serif';
const FONT_CODE = 'menlo, monaco, "Courier New", monospace';

const useStyles = makeStyles(theme => ({
  head: {
    position: 'absolute',
    top: 0,
    left: 0,
    right: 0,
    height: theme.TOOLBAR_HEIGHT,
    display: 'flex',
    flexDirection: 'row',
    flexWrap: 'nowrap',
    alignItems: 'stretch',
    backgroundColor: '#222',
  },

  home: {
    width: theme.TAB_WIDTH,
    height: theme.TOOLBAR_HEIGHT,
    paddingLeft: theme.spacing(2),
    display: 'flex',
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
  },

  tools: {
    height: theme.TOOLBAR_HEIGHT,
    flexGrow: 1,
    paddingRight: theme.spacing(5),
    display: 'flex',
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'right',
  },

  logo: {
    width: '36px',
    cursor: 'pointer',
    transition: 'opacity 0.5s',
    '&:hover': {
      opacity: '60%',
    },
  },

  nav: {
    position: 'absolute',
    top: theme.TOOLBAR_HEIGHT,
    bottom: 0,
    width: '300px',
    padding: theme.spacing(2),
    overflowY: 'scroll',
    scrollbarWidth: 'thin',
    '&::-webkit-scrollbar': {
      width: '8px',
      backgroundColor: '#303030',
    },
    '&::-webkit-scrollbar-thumb': {
      backgroundColor: '#383838',
    },
  },

  navTitle: {
    fontFamily: FONT_TITLE,
    fontSize: '1rem',
    fontWeight: 'lighter',
    paddingLeft: theme.spacing(2),
  },

  navItem: {
    height: '26px',
  },

  navItemText: {
    color: theme.palette.text.secondary,
    cursor: 'pointer',
    '&:hover': {
      textDecorationLine: 'underline',
    },
  },

  navItemTextParent: {
    color: theme.palette.text.primary,
    cursor: 'pointer',
    '&:hover': {
      textDecorationLine: 'underline',
    },
    fontWeight: 'bold',
  },

  navItemTextCurrent: {
    color: theme.palette.primary.main,
    cursor: 'pointer',
    '&:hover': {
      textDecorationLine: 'underline',
    },
    fontWeight: 'bold',
  },

  navGroup: {
    paddingLeft: theme.spacing(2),
  },

  toc: {
    position: 'fixed',
    top: theme.TOOLBAR_HEIGHT,
    width: '280px',
    right: '20px',
    padding: theme.spacing(2),
    borderLeft: '3px solid #505050',
    maxHeight: 'calc(100% - 100px)',
    overflow: 'auto',
    '&::-webkit-scrollbar': {
      width: '5px',
      backgroundColor: '#303030',
    },
    '&::-webkit-scrollbar-thumb': {
      backgroundColor: '#383838',
    },
  },

  tocTitle: {
    fontFamily: FONT_TITLE,
    fontSize: '1rem',
    fontWeight: 'lighter',
  },

  tocList: {
    listStyleType: 'none',
    paddingInlineStart: 0,
    marginInlineStart: 0,
    fontFamily: FONT_TOC,
    fontSize: '0.9rem',
    fontWeight: 'lighter',
  },

  tocListItem: {
    color: theme.palette.text.link,
    textDecorationLine: 'none',
    '&:hover': {
      textDecorationLine: 'underline',
    },
  },

  main: {
    position: 'absolute',
    top: theme.TOOLBAR_HEIGHT,
    bottom: 0,
    left: '300px',
    right: 0,
    padding: theme.spacing(2),
    overflowY: 'auto',
    '&::-webkit-scrollbar': {
      width: '8px',
      backgroundColor: '#303030',
    },
    '&::-webkit-scrollbar-thumb': {
      backgroundColor: '#383838',
    },
  },

  doc: {
    position: 'relative',
    paddingRight: '300px',
  },

  title: {
    fontFamily: FONT_TITLE,
    fontSize: '2.6rem',
  },

  h1: { fontFamily: FONT_TEXT, fontSize: '1.8rem', color: '#eec', fontWeight: 500 },
  h2: { fontFamily: FONT_TEXT, fontSize: '1.5rem', color: '#eec', fontWeight: 500 },
  h3: { fontFamily: FONT_TEXT, fontSize: '1.3rem', color: '#eec', fontWeight: 500 },
  h4: { fontFamily: FONT_TEXT, fontSize: '1.1rem', color: '#eec', fontWeight: 500 },
  h5: { fontFamily: FONT_TEXT, fontSize: '1.0rem', color: '#eec', fontWeight: 500 },

  p: {
    fontFamily: FONT_TEXT,
    fontSize: '16px',
    lineHeight: '26px',
    color: '#bbb',
  },

  strong: {
    fontWeight: 900,
    color: 'white',
  },

  table: {
    marginLeft: 'auto',
    marginRight: 'auto',
  },

  tip: {
    display: 'flex',
    flexDirection: 'row',
    alignItems: 'flex-start',
    padding: theme.spacing(2),
    margin: 0,
    backgroundColor: '#3e3e3e',
    borderLeftStyle: 'solid',
    borderLeftWidth: '3px',
    borderLeftColor: '#fff',
    borderRadius: '3px',
  },

  tipIcon: {
    flexGrow: 0,
    color: theme.palette.primary.main,
    paddingTop: theme.spacing(2),
    paddingRight: theme.spacing(2),
  },

  tipText: {
    flexGrow: 1,
    fontFamily: FONT_TEXT,
    fontSize: '1.0rem',
    lineHeight: '1.8rem',
  },

  link: {
    color: theme.palette.text.link,
    textDecorationLine: 'underline',
    textUnderlinePosition: 'under',
    '&:hover': {
      textDecorationLine: 'none',
    },
  },

  externalLinkIcon: {
    verticalAlign: 'sub',
  },

  codeBox: {
    fontFamily: FONT_CODE,
    fontSize: '100%',
    paddingTop: theme.spacing(2),
    paddingLeft: theme.spacing(2),
    paddingRight: theme.spacing(2),
    paddingBottom: 0,
    borderLeftStyle: 'solid',
    borderLeftWidth: '3px',
    borderLeftColor: '#ccf',
    borderRadius: '3px',
    overflow: 'auto',
  },

  codeSame: {
    opacity: '70%',
  },

  codeAdded: {
    backgroundColor: '#1e3e1e',
  },

  codeDeleted: {
    backgroundColor: '#3e1e1e',
    textDecorationLine: 'line-through',
  },

  inlineCode: {
    fontFamily: FONT_CODE,
    fontSize: '0.9rem',
    paddingLeft: theme.spacing(0.5),
    paddingRight: theme.spacing(0.5),
    paddingTop: '3px',
    paddingBottom: '3px',
    color: 'black',
    backgroundColor: '#ccf',
    borderRadius: '3px',
  },

  memberName: {
    fontFamily: FONT_CODE,
    fontSize: '0.9rem',
    fontWeight: 'bold',
    color: theme.palette.text.link,
    padding: theme.spacing(0.8),
    textDecorationLine: 'underline',
    textUnderlinePosition: 'under',
    '&:hover': {
      textDecorationLine: 'none',
    },
  },

  parameterName: {
    fontFamily: FONT_CODE,
    fontSize: '0.9rem',
    fontWeight: 'bold',
    color: theme.palette.text.primary,
    padding: theme.spacing(0.8),
  },

  description: {
    paddingInlineStart: theme.spacing(3),
    paddingBottom: theme.spacing(1.2),
    fontFamily: FONT_TEXT,
    fontSize: '16px',
    lineHeight: '26px',
    color: '#bbb',
  },

  selected: {
    color: theme.palette.primary.main,
  },
}));

const LANGS = ['en', 'zh', 'jp'];

const LANGNAMES = {
  'en': 'English',
  'zh': '中文',
  'jp': '日本語',
};

export const query = graphql`
  query(
    $id: String
    $name: String
    $parent: String
  ) {
    mdx(id: {eq: $id}) {
      frontmatter {
        title
      }
      body
      headings {
        depth
        value
      }
    }
    documentationJs(
      name: {eq: $name}
      memberof: {eq: $parent}
    ) {
      name
      memberof
      members {
        instance {
          name
          kind
          description {
            internal {
              content
            }
          }
        }
        static {
          name
          kind
          description {
            internal {
              content
            }
          }
        }
      }
      params {
        name
        description {
          internal {
            content
          }
        }
        optional
        default
      }
      returns {
        description {
          internal {
            content
          }
        }
      }
    }
    allMdx {
      nodes {
        fields {
          path
          lang
        }
        frontmatter {
          title
          api
        }
      }
    }
  }
`;

const makeStyledTag = (tag, style) => {
  style = style || tag;
  return ({ children, ...props }) => {
    const classes = useStyles();
    const Tag = tag;
    const className = classes[style];
    return (
      <Tag {...props} className={className}>
        {children}
      </Tag>
    );
  };
}

const DocContext = React.createContext();

const DocLink = ({ children, href }) => {
  const classes = useStyles();
  const { lang } = React.useContext(DocContext);
  if (href.startsWith('#')) {
    return (
      <Link to={href} className={classes.link}>
        {children}
      </Link>
    );
  } else if (href.startsWith('/')) {
    return (
      <Link to={`/docs/${lang}${href}`} className={classes.link}>
        {children}
      </Link>
    );
  } else {
    return (
      <a href={href} target="_blank" className={classes.link}>
        {children}
        {<ExternalLinkIcon fontSize="small" className={classes.externalLinkIcon}/>}
      </a>
    );
  }
}

const Tip = ({ children }) => {
  const classes = useStyles();
  return (
    <blockquote className={classes.tip}>
      <div className={classes.tipIcon}><TipIcon fontSize="large"/></div>
      <div className={classes.tipText}>{children}</div>
    </blockquote>
  )
}

const SourceCode = ({ children, className }) => {
  const classes = useStyles();
  const language = className === 'language-js' ? 'javascript' : undefined;
  const lines = children.split('\n');

  let isDiff = true;
  const colors = lines.map(
    line => {
      if (line.trim() === '') return;
      if (line.startsWith('  ')) return classes.codeSame;
      if (line.startsWith('+ ')) return classes.codeAdded;
      if (line.startsWith('- ')) return classes.codeDeleted;
      isDiff = false;
    }
  );

  if (isDiff) {
    children = lines.map(l => l.substring(2)).join('\n');
  }

  return (
    <Highlight {...defaultProps} code={children} language={language} theme={HighlightTheme}>
      {({ style, tokens, getLineProps, getTokenProps }) => (
        <pre className={classes.codeBox} style={{ ...style }}>
          {tokens.map((line, i) => {
            const props = getLineProps({ line, key: i });
            if (isDiff) props.className = colors[i];
            return (
              <div key={i} {...props}>
                {line.map((token, key) => (
                  <span key={key} {...getTokenProps({ token, key })} />
                ))}
              </div>
            );
          })}
        </pre>
      )}
    </Highlight>
  );
}

const Classes = () => {
  const classes = useStyles();
  let { jsdoc, lang, path } = React.useContext(DocContext);
  if (path.endsWith('/')) path = path.substring(0, path.length - 1);
  const members = jsdoc.members.static.filter(m => m.kind === 'class');
  return (
    members.map(
      m => (
        <React.Fragment>
          <Link
            className={classes.memberName}
            to={`/docs/${lang}/${path}/${m.name}`}
          >
            {m.name}
          </Link>
          <p className={classes.description}>
            {m.description?.internal?.content || '[No description]'}
          </p>
        </React.Fragment>
      )
    )
  );
}

const Constructor = () => {
  const classes = useStyles();
  let { jsdoc, lang, path } = React.useContext(DocContext);
  if (path.endsWith('/')) path = path.substring(0, path.length - 1);
  return (
    <React.Fragment>
      <Link
        className={classes.memberName}
        to={`/docs/${lang}/${path}/constructor`}
      >
        new {jsdoc.memberof ? jsdoc.memberof + '.' : ''}{jsdoc.name}()
      </Link>
      <p className={classes.description}>
        Create an instance of {jsdoc.name}.
      </p>
    </React.Fragment>
  );
}

const Properties = () => {
  const classes = useStyles();
  let { jsdoc, lang, path } = React.useContext(DocContext);
  if (path.endsWith('/')) path = path.substring(0, path.length - 1);
  const members = jsdoc.members.instance.filter(m => m.kind === 'member');
  return (
    members.map(
      m => (
        <React.Fragment>
          <Link
            className={classes.memberName}
            to={`/docs/${lang}/${path}/${m.name}`}
          >
            {m.name}
          </Link>
          <p className={classes.description}>
            {m.description?.internal?.content || '[No description]'}
          </p>
        </React.Fragment>
      )
    )
  );
}

const StaticProperties = () => {
  const classes = useStyles();
  let { jsdoc, lang, path } = React.useContext(DocContext);
  if (path.endsWith('/')) path = path.substring(0, path.length - 1);
  const members = jsdoc.members.static.filter(m => m.kind === 'member');
  return (
    members.map(
      m => (
        <React.Fragment>
          <Link
            className={classes.memberName}
            to={`/docs/${lang}/${path}/${m.name}`}
          >
            {m.name}
          </Link>
          <p className={classes.description}>
            {m.description?.internal?.content || '[No description]'}
          </p>
        </React.Fragment>
      )
    )
  );
}

const Methods = () => {
  const classes = useStyles();
  let { jsdoc, lang, path } = React.useContext(DocContext);
  if (path.endsWith('/')) path = path.substring(0, path.length - 1);
  const members = jsdoc.members.instance.filter(m => m.kind === 'function');
  return (
    members.map(
      m => (
        <React.Fragment>
          <Link
            className={classes.memberName}
            to={`/docs/${lang}/${path}/${m.name}`}
          >
            {m.name}()
          </Link>
          <p className={classes.description}>
            {m.description?.internal?.content || '[No description]'}
          </p>
        </React.Fragment>
      )
    )
  );
}

const StaticMethods = () => {
  const classes = useStyles();
  let { jsdoc, lang, path } = React.useContext(DocContext);
  if (path.endsWith('/')) path = path.substring(0, path.length - 1);
  const members = jsdoc.members.static.filter(m => m.kind === 'function');
  return (
    members.map(
      m => (
        <React.Fragment>
          <Link
            className={classes.memberName}
            to={`/docs/${lang}/${path}/${m.name}`}
          >
            {m.name}()
          </Link>
          <p className={classes.description}>
            {m.description?.internal?.content || '[No description]'}
          </p>
        </React.Fragment>
      )
    )
  );
}

const Parameters = () => {
  const classes = useStyles();
  const { jsdoc } = React.useContext(DocContext);
  const params = jsdoc.params;
  return (
    params.map(
      p => (
        <React.Fragment>
          <h6>
            <span className={classes.parameterName}>{p.name}</span>
          </h6>
          <p className={classes.description}>
            {p.description?.internal?.content || '[No description]'}
          </p>
        </React.Fragment>
      )
    )
  );
}

const ReturnValue = () => {
  const classes = useStyles();
  const { jsdoc } = React.useContext(DocContext);
  return (
    <p className={classes.description}>
      {jsdoc.returns[0].description.internal.content || '[No description]'}
    </p>
  );
}

const components = {
  h1: makeStyledTag('h1'),
  h2: makeStyledTag('h2'),
  h3: makeStyledTag('h3'),
  h4: makeStyledTag('h4'),
  h5: makeStyledTag('h5'),
  li: makeStyledTag('li', 'p'),
  p: makeStyledTag('p'),
  a: DocLink,
  strong: makeStyledTag('strong'),
  table: makeStyledTag('table'),
  blockquote: Tip,
  code: SourceCode,
  inlineCode: makeStyledTag('span', 'inlineCode'),

  Classes,
  Constructor,
  Properties,
  Methods,
  StaticProperties,
  StaticMethods,
  Parameters,
  ReturnValue,
};

const DocNavItem = ({ nodes, label, path, uri }) => {
  const classes = useStyles();
  const loc = useLocation();
  const { lang } = React.useContext(DocContext);
  if (!label && nodes) {
    let node = nodes.find(node => node?.fields?.path === path && node?.fields?.lang === lang);
    if (!node) node = nodes.find(node => node?.fields?.path === path);
    if (node) label = node.frontmatter.title;
  }
  if (!uri) uri = `/docs/${lang}/${path}`;
  let textClass = classes.navItemText;
  if (loc.pathname === uri) textClass = classes.navItemTextCurrent;
  else if (loc.pathname.startsWith(uri + '/')) textClass = classes.navItemTextParent;
  return (
    <ListItem key={path} className={classes.navItem}>
      <ListItemText
        disableTypography
        className={textClass}
        primary={label}
        onClick={() => navigate(uri)}
      />
    </ListItem>
  );
}

const docNavGroupExpandState = {};

const DocNavGroup = ({ label, path, uri, children }) => {
  const classes = useStyles();
  const loc = useLocation();
  const { lang } = React.useContext(DocContext);
  const baseUri = uri || `/docs/${lang}/${path}`;
  let textClass = classes.navItemText, expand = false;
  if (loc.pathname === uri) {
    textClass = classes.navItemTextCurrent;
  } else if (loc.pathname.startsWith(baseUri + '/')) {
    textClass = classes.navItemTextParent;
    expand = true;
  }
  const [open, setOpen] = React.useState(Boolean(docNavGroupExpandState[path] || expand));
  const handleClickExpand = () => {
    if (uri) navigate(uri);
    setOpen(!open);
    docNavGroupExpandState[path] = !open;
  }
  return (
    <React.Fragment>
      <ListItem key={path} className={classes.navItem}>
        <ListItemText
          disableTypography
          className={textClass}
          primary={label}
          onClick={handleClickExpand}
        />
        <IconButton size="small" onClick={handleClickExpand}>
          {open ? <ExpandLessIcon/> : <ExpandMoreIcon/>}
        </IconButton>
      </ListItem>
      <Collapse in={open}>
        <List dense disablePadding className={classes.navGroup}>
          {children}
        </List>
      </Collapse>
    </React.Fragment>
  )
}

const DocNavList = ({ nodes, prefix }) => {
  const { lang } = React.useContext(DocContext);
  const pages = nodes.filter(
    ({ fields, frontmatter: { api } }) => {
      if (!fields.path.startsWith(prefix)) return false;
      if (api) {
        const i = api.lastIndexOf('.');
        if (i >= 0) {
          const head = api[i + 1];
          if (head.toLowerCase() === head) {
            let name = api.substring(0, i);
            let j = name.lastIndexOf('.');
            if (j >= 0) name = name.substring(j + 1);
          }
        }
      } else if (fields.lang !== lang) {
        return false;
      }
      return true;
    }
  );

  const tree = {};
  pages.forEach(
    (page, i) => {
      const path = page.fields.path.substring(prefix.length);
      const segs = path.split('/');
      const name = segs.pop();
      let t = tree;
      for (const s of segs) {
        if (s) {
          const c = t.children || (t.children = {});
          t = c[s] || (c[s] = {});
        }
      }
      const c = t.children || (t.children = {});
      t = c[name] || (c[name] = {});
      t.id = i;
    }
  );

  return <DocNavTree prefix={prefix} pages={pages} tree={tree}/>;
}

const DocNavTree = ({ prefix, pages, tree }) => {
  const { lang } = React.useContext(DocContext);
  const children = tree.children;
  return (
    Object.keys(children).sort().map(
      k => {
        const v = children[k];
        let path, uri, label = k;
        if (typeof v.id === 'number') {
          const page = pages[v.id];
          label = page.frontmatter.title;
          path = page.fields.path;
          uri = `/docs/${lang}/${path}`;
          const i = label.lastIndexOf('.');
          if (i >= 0) label = label.substring(i + 1);
        }
        if (v.children) {
          const path = `${prefix}/${k}`;
          return (
            <DocNavGroup key={path} label={label} path={path} uri={uri}>
              <DocNavTree prefix={path} pages={pages} tree={v}/>
            </DocNavGroup>
          );
        } else {
          return <DocNavItem key={path} label={label} path={path} uri={uri}/>
        }
      }
    )
  );
}

const DocPage = ({ data }) => {
  const classes = useStyles();
  const loc = useLocation();

  let lang = 'en';
  let path = loc.pathname;

  if (path.startsWith('/docs/')) {
    path = path.substring(6);
    const i = path.indexOf('/');
    if (i >= 0) {
      const s = path.substring(0, i);
      if (LANGS.includes(s)) {
        lang = s;
        path = path.substring(i + 1);
      }
    }
  }

  const slugger = new Slugger;
  const nodes = data.allMdx.nodes;

  return (
    <DocContext.Provider value={{ jsdoc: data.documentationJs, lang, path }}>
      <div className={classes.head}>
        <div className={classes.home}>
          <img
            src={PipyLogo}
            alt="Home"
            className={classes.logo}
            onClick={() => navigate('/')}
          />
        </div>
        <div className={classes.tools}>
          <LangSelect/>
        </div>
      </div>
      <div className={classes.nav}>
        <div className={classes.logoBox}>
          <Typography component="h1" className={classes.navTitle}>
            Documentation
          </Typography>
        </div>
        <List dense>
          <DocNavGroup label="Introduction" path="intro">
            <DocNavItem nodes={nodes} path="intro/overview"/>
            <DocNavItem nodes={nodes} path="intro/concepts"/>
          </DocNavGroup>
          <DocNavGroup label="Getting Started" path="getting-started">
            <DocNavItem nodes={nodes} path="getting-started/build-install"/>
            <DocNavItem nodes={nodes} path="getting-started/quick-start"/>
            <DocNavItem nodes={nodes} path="getting-started/getting-help"/>
          </DocNavGroup>
          <DocNavGroup label="Tutorial" path="tutorial">
            <DocNavList nodes={nodes} prefix="tutorial"/>
          </DocNavGroup>
          <DocNavGroup label="Reference" path="reference">
            <DocNavList nodes={nodes} prefix="reference"/>
          </DocNavGroup>
        </List>
      </div>
      <div className={classes.main}>
        <div className={classes.doc}>
          <Typography component="h1" className={classes[`title_${lang}`] || classes.title}>
            {data.mdx.frontmatter.title}
          </Typography>
          <MDXProvider components={components}>
            <MDXRenderer headings={data.mdx.headings}>
              {data.mdx.body}
            </MDXRenderer>
          </MDXProvider>
        </div>
      </div>
      <div className={classes.toc}>
        <Typography component="h1" className={classes.tocTitle}>
          Table of Contents
        </Typography>
        <ul className={classes.tocList}>
          {data.mdx.headings.map(
            ({ value, depth }) => (
              <Link
                key={value}
                to={'#' + slugger.slug(value)}
                className={classes.tocListItem}
              >
                <Typography component="li" style={{ marginInlineStart: depth * 10 }}>
                  {value}
                </Typography>
              </Link>
            )
          )}
        </ul>
      </div>
    </DocContext.Provider>
  );
}

const LangSelect = () => {
  const classes = useStyles();
  const { lang, path } = React.useContext(DocContext);
  const [ anchorEl, setAnchorEl ] = React.useState(null);

  const handleClick = event => {
    setAnchorEl(event.currentTarget);
  };

  const handleClose = () => {
    setAnchorEl(null);
  };

  return (
    <React.Fragment>
      <Button
        startIcon={<LangIcon/>}
        endIcon={<ExpandMoreIcon/>}
        onClick={handleClick}
      >
        {LANGNAMES[lang]}
      </Button>
      <Popover
        open={Boolean(anchorEl)}
        anchorEl={anchorEl}
        onClose={handleClose}
        anchorOrigin={{
          vertical: 'bottom',
          horizontal: 'right',
        }}
        transformOrigin={{
          vertical: 'top',
          horizontal: 'right',
        }}
      >
        <List dense>
          {LANGS.map(
            l => (
              <ListItem key={l} button onClick={() => navigate(`/docs/${l}/${path}`)}>
                <ListItemText
                  className={lang === l ? classes.selected : undefined}
                  primary={LANGNAMES[l]}
                />
              </ListItem>
            )
          )}
        </List>
      </Popover>
    </React.Fragment>
  );
}

export default DocPage;
