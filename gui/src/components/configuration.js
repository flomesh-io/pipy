import React from 'react';
import GlobalState from '../global-state';

import { makeStyles } from '@material-ui/core/styles';

// Material-UI components
import List from '@material-ui/core/List';
import ListItem from '@material-ui/core/ListItem';
import ListItemIcon from '@material-ui/core/ListItemIcon';
import ListItemText from '@material-ui/core/ListItemText';

// Components
import Console from './console';
import Flowchart from './flowchart';
import Nothing from './nothing';
import Pane from 'react-split-pane/lib/Pane';
import SplitPane from 'react-split-pane';
import Toolbar from './toolbar';

// Icons
import ModuleIcon from '@material-ui/icons/DescriptionSharp';
import PipelineIcon from '@material-ui/icons/InputSharp';

const useStyles = makeStyles(theme => ({
  root: {
    width: '100%',
    height: '100%',
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'stretch',
  },
  main: {
    height: `calc(100% - ${theme.TOOLBAR_HEIGHT}px)`,
  },
  pipelineListPane: {
    height: '100%',
    backgroundColor: '#282828',
    padding: theme.spacing(1),
    overflow: 'auto',
  },
  flowchartPane: {
    height: '100%',
    backgroundColor: '#202020',
    padding: theme.spacing(1),
    overflow: 'auto',
  },
  consolePane: {
    height: '100%',
    backgroundColor: '#202020',
  },
  listIcon: {
    minWidth: 36,
  },
  pipelineList: {
    marginLeft: theme.spacing(3),
  },
}));

// Global state
const splitPos = [
  ['260px', 100],
  [1, 1],
];
let selectedModule = '';
let selectedPipeline = 0;

function Configuration() {
  const classes = useStyles();
  const globalState = React.useContext(GlobalState);

  const [ config, setConfig ] = React.useState(null);
  const [ currentModule, setCurrentModule ] = React.useState(selectedModule);
  const [ currentPipeline, setCurrentPipeline ] = React.useState(selectedPipeline);

  React.useEffect(
    () => void (
      async () => {
        if (!config) {
          globalState.showWaiting('Loading configuration...');
          try {
            const res = await fetch('/api/config');
            if (res.status === 200) {
              setConfig(await res.json());
            }
          } finally {
            globalState.showWaiting(null);
          }    
        }
      }
    )(),
    [config, globalState]
  );

  // const loadConfig = async () => {
  //   globalState.showWaiting('Loading configuration...');
  //   try {
  //     const res = await fetch('/api/config');
  //     if (res.status === 200) {
  //       setConfig(await res.json());
  //     }
  //   } finally {
  //     globalState.showWaiting(null);
  //   }
  // }

  const selectPipeline = (mod, i) => {
    selectedModule = mod;
    selectedPipeline = i;
    setCurrentModule(mod);
    setCurrentPipeline(i);
  }

  const notRunning = !globalState.runningProgram;

  return (
    <div className={classes.root}>

      {/* Toolbar */}
      <Toolbar/>

      {/* Main View */}
      <div className={classes.main}>
        <SplitPane split="vertical" onChange={pos => splitPos[0] = pos}>

          {/* Pipeline List */}
          <Pane initialSize={splitPos[0][0]} className={classes.pipelineListPane}>
            {notRunning ? (
              <Nothing text="No program running"/>
            ) : (
              <List dense disablePadding>
                {Object.entries(config || {}).map(([name, mod]) => (
                  <React.Fragment key={name}>
                    <ListItem disableGutters disabled>
                      <ListItemIcon className={classes.listIcon}><ModuleIcon/></ListItemIcon>
                      <ListItemText primary={name}/>
                    </ListItem>
                    <List dense disablePadding className={classes.pipelineList}>
                      {mod.roots.map(i => (
                        <ListItem
                          key={i}
                          button
                          selected={name === currentModule && i === currentPipeline}
                          onClick={() => selectPipeline(name, i)}
                        >
                          <ListItemIcon className={classes.listIcon}><PipelineIcon/></ListItemIcon>
                          <ListItemText primary={mod.nodes[i].name}/>
                        </ListItem>
                      ))}
                    </List>
                  </React.Fragment>
                ))}
              </List>
            )}
          </Pane>

          <SplitPane initialSize={splitPos[0][1]} onChange={pos => splitPos[1] = pos}>

            {/* Flowchart */}
            <Pane initialSize={splitPos[1][0]} className={classes.flowchartPane}>
              {
                (notRunning && (
                  <Nothing text="No program running"/>
                )) ||
                (!config?.[currentModule] && (
                  <Nothing text="No pipeline selected"/>
                )) || (
                  <Flowchart nodes={config?.[currentModule]?.nodes} root={currentPipeline}/>
                )
              }
            </Pane>

            {/* Console */}
            <Pane initialSize={splitPos[1][1]} className={classes.consolePane}>
              <Console/>
            </Pane>

          </SplitPane>
        </SplitPane>
      </div>
    </div>
  );
}

export default Configuration;