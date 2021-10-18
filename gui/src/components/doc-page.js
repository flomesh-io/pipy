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

const FONT_TITLE = 'rockwell,palatino,serif';
const FONT_TOC = 'verdana';
const FONT_TEXT = 'arial,sans-serif';
const FONT_CODE = 'menlo,monaco,"Courier New",monospace';

const useStyles = makeStyles(theme => ({
  root: {
    position: 'absolute',
    top: 0,
    bottom: 0,
    left: 0,
    right: 0,
    display: 'flex',
    flexDirection: 'column',
    flexWrap: 'nowrap',
    alignItems: 'stretch',
    overflow: 'hidden',
  },

  head: {
    minHeight: theme.TOOLBAR_HEIGHT,
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

  body: {
    display: 'flex',
    paddingTop: theme.spacing(2),
    flexDirection: 'row',
    flexWrap: 'nowrap',
    alignItems: 'flex-start',
    overflowX: 'hidden',
    overflowY: 'scroll',
  },

  nav: {
    minWidth: '300px',
    padding: theme.spacing(1),
  },

  navTitle: {
    fontFamily: FONT_TITLE,
    fontSize: '1rem',
    fontWeight: 'lighter',
    paddingLeft: theme.spacing(2),
  },

  navItem: {
    color: theme.palette.text.secondary,
    cursor: 'pointer',
    '&:hover': {
      textDecorationLine: 'underline',
    },
  },

  navItemCurrent: {
    color: theme.palette.text.primary,
    cursor: 'pointer',
    '&:hover': {
      textDecorationLine: 'underline',
    },
  },

  navGroup: {
    paddingLeft: theme.spacing(2),
  },

  toc: {
    minWidth: '300px',
    padding: theme.spacing(1),
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
    flexGrow: 1,
    paddingBottom: theme.spacing(5),
  },

  title: {
    fontFamily: FONT_TITLE,
    fontSize: '3rem',
  },

  h1: { fontFamily: FONT_TITLE, fontSize: '2.0rem' },
  h2: { fontFamily: FONT_TITLE, fontSize: '1.5rem', fontWeight: 'lighter' },
  h3: { fontFamily: FONT_TITLE, fontSize: '1.3rem', fontWeight: 'lighter' },
  h4: { fontFamily: FONT_TITLE, fontSize: '1.1rem', fontWeight: 'lighter' },

  p: {
    fontFamily: FONT_TEXT,
    fontSize: '1.0rem',
    lineHeight: '1.8rem',
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
    borderLeftColor: theme.palette.primary.main,
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
    fontSize: '1.0rem',
    paddingLeft: theme.spacing(0.5),
    paddingRight: theme.spacing(0.5),
    paddingTop: '3px',
    paddingBottom: '3px',
    backgroundColor: '#1e1e1e',
  },

  memberName: {
    fontFamily: FONT_CODE,
    fontSize: '1.0rem',
    fontWeight: 'bold',
    color: theme.palette.text.link,
    padding: theme.spacing(0.8),
    backgroundColor: theme.palette.text.codeBox,
    textDecorationLine: 'underline',
    textUnderlinePosition: 'under',
    '&:hover': {
      textDecorationLine: 'none',
    },
  },

  parameterName: {
    fontFamily: FONT_CODE,
    fontSize: '1.0rem',
    fontWeight: 'bold',
    color: theme.palette.text.primary,
    padding: theme.spacing(0.8),
    backgroundColor: theme.palette.text.codeBox,
  },

  description: {
    paddingInlineStart: theme.spacing(3),
    fontFamily: FONT_TEXT,
    fontSize: '1.0rem',
  },

  selected: {
    color: theme.palette.primary.main,
  },
}));

const LANGS = ['en', 'zh'];

const LANGNAMES = {
  'en': 'English',
  'zh': '中文',
};

export const query = graphql`
  query(
    $id: String
    $kind: String
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
      kind: {eq: $kind}
      name: {eq: $name}
      memberof: {eq: $parent}
    ) {
      name
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
    return (
      <Tag {...props} className={classes[style]}>
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
    )
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
  return (
    <Highlight {...defaultProps} code={children} language={language} theme={HighlightTheme}>
      {({ style, tokens, getLineProps, getTokenProps }) => (
        <pre className={classes.codeBox} style={{ ...style }}>
          {tokens.map((line, i) => {
            const props = getLineProps({ line, key: i });
            let head = line[0];
            if (head?.content === '') head = line[1];
            switch (head?.content) {
              case '+':
                props.className = classes.codeAdded;
                line.content = ' ';
                break;
              case '-':
                props.className = classes.codeDeleted;
                line.content = ' ';
                break;
            }
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

const InstanceMethods = () => {
  const classes = useStyles();
  const { jsdoc, lang } = React.useContext(DocContext);
  const members = jsdoc.members.instance.filter(m => m.kind === 'function');
  return (
    members.map(
      m => (
        <React.Fragment>
          <Link
            className={classes.memberName}
            to={`/docs/${lang}/reference/classes/${jsdoc.name}/${m.name}`}
          >
            {m.name}()
          </Link>
          <p className={classes.description}>
            {m.description.internal.content}
          </p>
        </React.Fragment>
      )
    )
  );
}

const FunctionParameters = () => {
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
            {p.description.internal.content}
          </p>
        </React.Fragment>
      )
    )
  );
}

const FunctionReturns = () => {
  const classes = useStyles();
  const { jsdoc } = React.useContext(DocContext);
  return (
    <p className={classes.description}>
      {jsdoc.returns[0].description.internal.content}
    </p>
  );
}

const components = {
  h1: makeStyledTag('h1'),
  h2: makeStyledTag('h2'),
  h3: makeStyledTag('h3'),
  h4: makeStyledTag('h4'),
  li: makeStyledTag('li', 'p'),
  p: makeStyledTag('p'),
  a: DocLink,
  blockquote: Tip,
  code: SourceCode,
  inlineCode: makeStyledTag('span', 'inlineCode'),

  InstanceMethods,
  FunctionParameters,
  FunctionReturns,
};

const DocNavItem = ({ label, path }) => {
  const { lang } = React.useContext(DocContext);
  const classes = useStyles();
  const loc = useLocation();
  const uri = `/docs/${lang}/${path}`;
  return (
    <ListItem key={path}>
      <ListItemText
        className={loc.pathname === uri ? classes.navItemCurrent : classes.navItem}
        primary={label}
        onClick={() => navigate(uri)}
      />
    </ListItem>
  );
}

const docNavGroupExpandState = {};

const DocNavGroup = ({ label, path, children }) => {
  const classes = useStyles();
  const [open, setOpen] = React.useState(Boolean(docNavGroupExpandState[path]));
  const handleClickExpand = () => {
    setOpen(!open);
    docNavGroupExpandState[path] = !open;
  }
  return (
    <React.Fragment>
      <ListItem key={path}>
        <ListItemText
          className={classes.navItem}
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
  const pages = nodes.filter(
    ({ fields: { path }, frontmatter: { api } }) => {
      if (!path.startsWith(prefix)) return false;
      if (api && api.endsWith('()')) {
        const head = api[0];
        if (head.toUpperCase() === head) return false;
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
      for (const s of segs) if (s) t = t[s] || (t[s] = {});
      t[name] = i;
    }
  );

  return <DocNavTree prefix={prefix} pages={pages} tree={tree}/>;
}

const DocNavTree = ({ prefix, pages, tree }) => {
  return (
    Object.keys(tree).sort().map(
      k => {
        const v = tree[k];
        if (typeof v === 'number') {
          const { fields: { path }, frontmatter: { title }} = pages[v];
          return <DocNavItem key={path} label={title} path={path}/>
        } else {
          const path = `${prefix}/${k}`;
          return (
            <DocNavGroup key={path} path={path} label={k}>
              <DocNavTree prefix={path} pages={pages} tree={v}/>
            </DocNavGroup>
          );
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

  return (
    <DocContext.Provider value={{ jsdoc: data.documentationJs, lang, path }}>
      <div className={classes.root}>
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
        <div className={classes.body}>
          <div className={classes.nav}>
            <div className={classes.logoBox}>
              <Typography component="h1" className={classes.navTitle}>
                Documentation
              </Typography>
            </div>
            <List dense>
              <DocNavItem label="Overview" path="overview"/>
              <DocNavItem label="Quick Start" path="quick-start"/>
              <DocNavItem label="Concepts" path="concepts"/>
              <DocNavGroup label="Tutorial" path="tutorial">
                <DocNavList nodes={data.allMdx.nodes} prefix="tutorial"/>
              </DocNavGroup>
              <DocNavGroup label="Reference" path="reference">
                <DocNavGroup label="Functions" path="reference/functions">
                  <DocNavList nodes={data.allMdx.nodes} prefix="reference/functions"/>
                </DocNavGroup>
                <DocNavGroup label="Classes" path="reference/classes">
                  <DocNavList nodes={data.allMdx.nodes} prefix="reference/classes"/>
                </DocNavGroup>
              </DocNavGroup>
            </List>
          </div>
          <div className={classes.main}>
            <Typography component="h1" className={classes.title}>
              {data.mdx.frontmatter.title}
            </Typography>
            <MDXProvider components={components}>
              <MDXRenderer headings={data.mdx.headings}>
                {data.mdx.body}
              </MDXRenderer>
            </MDXProvider>
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
        </div>
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
