import React from 'react';

import { makeStyles } from '@material-ui/core/styles';
import { graphql, navigate, Link } from 'gatsby';
import { useQuery } from 'react-query';

// Material-UI components
import Box from '@material-ui/core/Box';
import Button from '@material-ui/core/Button';
import Container from '@material-ui/core/Container';
import Grid from '@material-ui/core/Grid';
import TreeView from '@material-ui/lab/TreeView';
import TreeItem from '@material-ui/lab/TreeItem';
import Typography from '@material-ui/core/Typography';

// Components
import Codebase from '../components/codebase';
import DialogNewCodebase from '../components/dialog-new-codebase';
import Loading from '../components/loading';

// Icons
import AddCircleIcon from '@material-ui/icons/AddCircle';
import ArrowRightIcon from '@material-ui/icons/AddBox';
import ArrowDownIcon from '@material-ui/icons/IndeterminateCheckBox';
import CodebaseIcon from '@material-ui/icons/ListAlt';
import ExternalLinkIcon from '@material-ui/icons/OpenInNewSharp';

// Logo
import PipyLogo from '../images/pipy.svg';

// CSS styles
const useStyles = makeStyles(theme => ({
  root: {
    marginTop: theme.spacing(1),
    display: 'flex',
    flexDirection: 'row',
    flexWrap: 'nowrap',
    alignItems: 'start',
  },
  left: {
    flexBasis: '50%',
    flexGrow: 1,
  },
  divider: {
    height: '600px',
    borderLeftStyle: 'solid',
    borderLeftColor: '#666',
    borderLeftWidth: '2px',
    marginTop: theme.spacing(3),
    paddingLeft: theme.spacing(3),
  },
  right: {
    flexBasis: '50%',
    flexGrow: 1,
    paddingTop: theme.spacing(1),
  },
  logo: {
    width: '160px',
  },
  listItem: {
    paddingRight: theme.spacing(2),
  },
  basePath: {
    color: '#888',
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
}));

export const query = graphql`
  query {
    allDts {
      nodes {
        filename
        internal {
          content
        }
      }
    }
  }
`;

let storedTreeExpanded = [];

//
// Landing page
//

function Index({ data }) {
  const classes = useStyles();

  const [openDialogNewCodebase, setOpenDialogNewCodebase] = React.useState(false);
  const [treeExpanded, setTreeExpanded] = React.useState(storedTreeExpanded);

  const queryCodebaseList = useQuery(
    'codebases',
    async () => {
      const res = await fetch('/api/v1/repo');
      if (res.status !== 200) {
        const msg = await res.text();
        throw new Error(`Error: ${msg}, status = ${res.status}`);
      }
      const lines = await res.text();
      const paths = lines.split('\n').filter(l => Boolean(l)).sort();
      return paths;
    },
    {
      retry: false,
      refetchOnWindowFocus: false,
      refetchOnReconnect: false,
    }
  );

  const handleExpandFolder = list => {
    setTreeExpanded(list);
    storedTreeExpanded = list;
  };

  if (queryCodebaseList.isLoading) {
    return (
      <div className={classes.root}>
        <Loading/>
      </div>
    );
  }

  if (!queryCodebaseList.isSuccess) {
    const dts = Object.fromEntries(
      data.allDts.nodes.map(
        ({ filename, internal }) => [filename, internal.content]
      )
    );  
    return <Codebase root="/" dts={dts}/>;
  }

  const tree = {};
  const populateTree = path => {
    const segs = path.split('/').filter(s => Boolean(s));
    const name = segs.pop();
    let parent = tree;
    for (const seg of segs) {
      parent = parent[seg] || (parent[seg] = {});
    }
    parent[name] = path;
  }

  if (queryCodebaseList.data) {
    queryCodebaseList.data.forEach(
      path => populateTree(path)
    );
  }

  return (
    <Container>
    <div className={classes.root}>
      <div className={classes.left}>
        <img src={PipyLogo} alt='PipyLogo' className={classes.logo}/>
        <Typography>
          <p>Welcome to Pipy! - A programmable network proxy for the cloud, edge and IoT.</p>
          <h1>Getting started</h1>
          <ul>
            <li><DocLink href="/tutorial/01-hello">Start Tutorial</DocLink> - Learn Pipy step by step.</li>
          </ul>
          <h1>Documentation</h1>
          <ul>
            <li><DocLink href="/intro/overview">Documentation</DocLink> - Official Pipy documentation.</li>
            <li><DocLink href="/reference/api">API Reference</DocLink> - Reference of Pipy API.</li>
            <li><DocLink href="/reference/pjs">PipyJS Reference</DocLink> - Reference of PipyJS - The JavaScript dialect used in Pipy.</li>
          </ul>
          <h1>Resources</h1>
          <ul>
            <li><DocLink href="http://flomesh.io/">flomesh.io</DocLink> - Visit the official website of Pipy.</li>
            <li><DocLink href="https://blog.flomesh.io/">Blog</DocLink> - Let's talk about Pipy.</li>
            <li><DocLink href="https://github.com/flomesh-io/pipy">GitHub</DocLink> - Meet the community.</li>
          </ul>
        </Typography>
      </div>
      <div className={classes.divider}/>
      <div className={classes.right}>
        <Typography>
          <h1>Codebases</h1>
        </Typography>
        <Box pb={2}>
          <Grid container spacing={3}>
            <Grid item>
              <Button
                variant="text"
                startIcon={<AddCircleIcon/>}
                onClick={() => setOpenDialogNewCodebase(true)}
              >
                New Codebase
              </Button>
            </Grid>
          </Grid>
        </Box>
        <DialogNewCodebase
          open={openDialogNewCodebase}
          onSuccess={path => navigate(`/repo${path}/`)}
          onClose={() => setOpenDialogNewCodebase(false)}
        />
        <Box p={1}>
          <TreeView
            disableSelection
            defaultEndIcon={<CodebaseIcon/>}
            defaultCollapseIcon={<ArrowDownIcon/>}
            defaultExpandIcon={<ArrowRightIcon/>}
            defaultExpanded={['/']}
            expanded={treeExpanded}
            onNodeToggle={(_, list) => handleExpandFolder(list)}
            onNodeSelect={() => {}}
          >
            {Object.keys(tree).sort().map(
              name => (
                <CodebaseItem
                  key={name}
                  path={'/' + name}
                  tree={tree[name]}
                />
              )
            )}
          </TreeView>
        </Box>
      </div>
    </div>
    </Container>
  );
}

function CodebaseItem({ path, tree }) {
  const classes = useStyles();
  const i = path.lastIndexOf('/');
  const base = path.substring(0,i+1);
  const name = path.substring(i+1);
  if (typeof tree === 'string') {
    return (
      <TreeItem
        key={path}
        nodeId={path}
        label={
          <Typography noWrap className={classes.listItem}>
            <span className={classes.basePath}>{base}</span>
            <span>{name}</span>
          </Typography>
        }
        onClick={() => navigate(`/repo${path}/`)}
      />
    );
  } else {
    const keys = Object.keys(tree).sort();
    return (
      <TreeItem
        key={path}
        nodeId={path}
        label={
          <Typography noWrap className={classes.listItem}>
            <span className={classes.basePath}>{base}</span>
            <span>{name}/</span>
          </Typography>
        }
      >
        {keys.map(key => (
          <CodebaseItem
            key={key}
            path={path + '/' + key}
            tree={tree[key]}
          />
        ))}
      </TreeItem>
    );
  }
}

const DocLink = ({ children, href }) => {
  const classes = useStyles();
  const lang = 'en';
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

export default Index;
