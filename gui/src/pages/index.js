import React from 'react';
import GlobalState from '../global-state';

import { makeStyles } from '@material-ui/core/styles';

// Material-UI components
import CircularProgress from '@material-ui/core/CircularProgress';
import Dialog from '@material-ui/core/Dialog';
import DialogContent from '@material-ui/core/DialogContent';
import List from '@material-ui/core/List';
import ListItem from '@material-ui/core/ListItem';
import ListItemIcon from '@material-ui/core/ListItemIcon';
import Typography from '@material-ui/core/Typography';

// Components
import Console from '../components/console';
import Configuration from '../components/configuration';
import Editor from '../components/editor';

// Icons
import ConfigurationIcon from '@material-ui/icons/AccountTreeSharp';
import ConsoleIcon from '@material-ui/icons/DvrSharp';
import FolderIcon from '@material-ui/icons/FolderSharp';

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
    paddingTop: theme.TOOLBAR_HEIGHT,
  },
  main: {
    width: `calc(100% - ${theme.TAB_WIDTH}px)`,
  },
}));

//
// Main interface
//

function Index() {
  const classes = useStyles();

  const [runningProgram, setRunningProgram] = React.useState(null);
  const [logTextNode, setLogTextNode] = React.useState(null);
  const [tab, setTab] = React.useState('configuration');
  const [showWaiting, setShowWaiting] = React.useState(false);
  const [waitingInfo, setWaitingInfo] = React.useState('');

  // Fetch current running program
  React.useEffect(
    () => {
      (async () => {
        const res = await fetch('/api/program');
        if (res.status === 200) {
          setRunningProgram(await res.text());
        }
      })();
    },
    []
  );

  // Regularly fetch the log data
  React.useEffect(
    () => {
      let running = true;
      let size = 0;
      const node = document.createTextNode('');
      const poll = async () => {
        try {
          const res = await fetch('/api/log', {
            headers: {
              'x-log-size': size,
            },
          });
          if (res.status === 200) {
            const more = parseInt(res.headers.get('x-log-size')) - size;
            if (more > 0) {
              const text = await res.text();
              const lines = text.split('\n').slice(-more-1);
              size += more;
              node.appendData(lines.join('\n'));
              const parentEl = node.parentElement;
              if (parentEl) parentEl.scrollIntoView(false);
              next(100);
              return;
            }
          }
          next(1000);
        } catch (err) {
          next(1000);
        }
      };
      const next = delay => {
        if (running) {
          window.setTimeout(poll, delay);
        }
      };
      setLogTextNode(node);
      next(1000);
      return () => running = false;
    },
    []
  );

  const showWaitingInfo = (msg) => {
    if (msg) {
      setShowWaiting(true);
      setWaitingInfo(msg);
    } else {
      setShowWaiting(false);
    }
  }

  return (
    <GlobalState.Provider
      value={{
        runningProgram,
        setRunningProgram,
        logTextNode,
        clearLog: () => logTextNode && (logTextNode.data = ''),
        showWaiting: showWaitingInfo,
      }}
    >

      {/* Root */}
      <div className={classes.root}>

        {/* Tabs */}
        <div className={classes.tab}>
          <List>
            <ListItem button key="configuration" selected={tab === 'configuration'} onClick={() => setTab('configuration')}>
              <ListItemIcon><ConfigurationIcon/></ListItemIcon>
            </ListItem>
            <ListItem button key="editor" selected={tab === 'editor'} onClick={() => setTab('editor')}>
              <ListItemIcon><FolderIcon/></ListItemIcon>
            </ListItem>
            <ListItem button key="console" selected={tab === 'console'} onClick={() => setTab('console')}>
              <ListItemIcon><ConsoleIcon/></ListItemIcon>
            </ListItem>
          </List>
        </div>

        {/* Tab Page */}
        <div className={classes.main}>
          {tab === 'editor' && <Editor/>}
          {tab === 'configuration' && <Configuration/>}
          {tab === 'console' && <Console/>}
        </div>
      </div>

      {/* Waiting Dialog */}
      <Dialog open={showWaiting}>
        <DialogContent>
          <CircularProgress/>
          <Typography>{waitingInfo}</Typography>
        </DialogContent>
      </Dialog>

    </GlobalState.Provider>
  );
}

export default Index;