import React from 'react';

import { makeStyles } from '@material-ui/core/styles';
import { navigate } from 'gatsby';
import { InstanceContext } from './instances';

// Material-UI components
import List from '@material-ui/core/List';
import ListItem from '@material-ui/core/ListItem';
import ListItemIcon from '@material-ui/core/ListItemIcon';

// Components
import Console from './console';
import Editor from './editor';
import Logs from './logs';
import Metrics from './metrics';
import Status from './status';

// Icons
import ConfigurationIcon from '@material-ui/icons/AccountTreeSharp';
import ConsoleIcon from '@material-ui/icons/DvrSharp';
import FolderIcon from '@material-ui/icons/FolderSharp';
import MetricsIcon from '@material-ui/icons/ShowChartSharp';

// Logo
import PipyLogo from '../images/pipy.svg';

// CSS styles
const useStyles = makeStyles(theme => ({
  root: {
    position: 'absolute',
    top: 0,
    bottom: 0,
    left: 0,
    right: 0,
    display: 'flex',
    flexDirection: 'row',
    flexWrap: 'nowrap',
    alignItems: 'stretch',
  },
  tab: {
    width: theme.TAB_WIDTH,
  },
  home: {
    width: theme.TAB_WIDTH,
    height: theme.TOOLBAR_HEIGHT,
    display: 'flex',
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
  },
  logo: {
    width: '26px',
    cursor: 'pointer',
    transition: 'opacity 0.5s',
    '&:hover': {
      opacity: '60%',
    },
  },
  main: {
    width: `calc(100% - ${theme.TAB_WIDTH}px)`,
  },
}));

//
// Global states
//

const codebaseStates = {};

//
// Codebase interface
//

function Codebase({ root, dts }) {
  if (root !== '/' && root.endsWith('/')) {
    root = root.substring(0, root.length - 1);
  }

  const classes = useStyles();

  const states = codebaseStates[root] || (
    codebaseStates[root] = {
      tab: 'editor',
    }
  );

  const [tab, setTab] = React.useState(states.tab);
  const [currentInstance, setCurrentInstance] = React.useState(null);
  const [currentInstanceIndex, setCurrentInstanceIndex] = React.useState();

  const handleClickTab = tab => {
    setTab(tab);
    states.tab = tab;
  }

  return (
    <div className={classes.root}>

      {/* Tabs */}
      <div className={classes.tab}>
        <div className={classes.home}>
          <img
            src={PipyLogo}
            alt="Home"
            className={classes.logo}
            onClick={() => navigate('/')}
          />
        </div>
        <List>
          <ListItem button key="editor" selected={tab === 'editor'} onClick={() => handleClickTab('editor')}>
            <ListItemIcon><FolderIcon/></ListItemIcon>
          </ListItem>
          <ListItem button key="status" selected={tab === 'status'} onClick={() => handleClickTab('status')}>
            <ListItemIcon><ConfigurationIcon/></ListItemIcon>
          </ListItem>
          <ListItem button key="metrics" selected={tab === 'metrics'} onClick={() => handleClickTab('metrics')}>
            <ListItemIcon><MetricsIcon/></ListItemIcon>
          </ListItem>
          <ListItem button key="logs" selected={tab === 'logs'} onClick={() => handleClickTab('logs')}>
            <ListItemIcon><ConsoleIcon/></ListItemIcon>
          </ListItem>
        </List>
      </div>

      {/* Tab Page */}
      <InstanceContext.Provider
        value={{
          currentInstance,
          currentInstanceIndex,
          setCurrentInstance,
          setCurrentInstanceIndex,
        }}
      >
        <div className={classes.main}>
          {tab === 'editor' && <Editor root={root} dts={dts}/>}
          {tab === 'status' && <Status root={root}/>}
          {tab === 'metrics' && <Metrics root={root}/>}
          {tab === 'logs' && <Logs root={root}/>}
        </div>
      </InstanceContext.Provider>

    </div>
  );
}

export default Codebase;
