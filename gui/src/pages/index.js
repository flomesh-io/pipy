import React from 'react';

import { makeStyles } from '@material-ui/core/styles';
import { navigate } from 'gatsby';
import { useQuery } from 'react-query';

// Material-UI components
import Box from '@material-ui/core/Box';
import Button from '@material-ui/core/Button';
import Grid from '@material-ui/core/Grid';
import OutlinedInput from '@material-ui/core/OutlinedInput';
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
import HelpIcon from '@material-ui/icons/HelpSharp';
import SearchIcon from '@material-ui/icons/SearchSharp';

// Logo
import PipyLogo from '../images/pipy.svg';

// CSS styles
const useStyles = makeStyles(theme => ({
  root: {
    paddingTop: '220px',
    display: 'flex',
    flexDirection: 'column',
    flexWrap: 'nowrap',
    alignItems: 'center',
  },
  logo: {
    position: 'absolute',
    width: '160px',
    left: 'calc(50% - 80px)',
    top: theme.spacing(1),
  },
  listItem: {
    paddingRight: theme.spacing(2),
  },
  basePath: {
    color: '#888',
  },
}));

let storedTreeExpanded = [];

//
// Landing page
//

function Index() {
  const classes = useStyles();

  const [openDialogNewCodebase, setOpenDialogNewCodebase] = React.useState(false);
  const [treeExpanded, setTreeExpanded] = React.useState(storedTreeExpanded);

  const queryCodebaseList = useQuery(
    'codebases',
    async () => {
      const res = await fetch('/api/v1/repo');
      if (res.status !== 200) {
        console.log('ERROR', res.status)
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
    return <Codebase root="/"/>;
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
    console.log(tree);
  }

  return (
    <div className={classes.root}>
      <img src={PipyLogo} alt='PipyLogo' className={classes.logo}/>
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
          <Grid item>
            <Button
              variant="text"
              startIcon={<HelpIcon/>}
              onClick={() => navigate('/docs')}
            >
              Documentation
            </Button>
          </Grid>
        </Grid>
      </Box>
      <DialogNewCodebase
        open={openDialogNewCodebase}
        onSuccess={path => navigate(`/repo${path}/`)}
        onClose={() => setOpenDialogNewCodebase(false)}
      />
      <OutlinedInput
        startAdornment={<SearchIcon/>}
        placeholder="Search..."
        margin="dense"
      />
      <Box p={2}>
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

export default Index;
