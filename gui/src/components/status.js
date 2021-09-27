import React from 'react';

import { makeStyles } from '@material-ui/core/styles';
import { useQuery } from 'react-query';

// Material-UI components
import List from '@material-ui/core/List';
import ListItem from '@material-ui/core/ListItem';
import ListItemIcon from '@material-ui/core/ListItemIcon';
import ListItemText from '@material-ui/core/ListItemText';
import Typography from '@material-ui/core/Typography';

// Components
import Flowchart from './flowchart';
import Nothing from './nothing';
import Pane from 'react-split-pane/lib/Pane';
import SplitPane from 'react-split-pane';
import TimeAgo from 'react-timeago';
import Toolbar from './toolbar';

// Icons
import LocalHostIcon from '@material-ui/icons/DesktopWindowsSharp';
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
  instanceListPane: {
    height: '100%',
    backgroundColor: '#282828',
    padding: theme.spacing(1),
    overflow: 'auto',
  },
  instanceActive: {
    color: '#0c0',
  },
  pipelineListPane: {
    height: '100%',
    backgroundColor: '#252525',
    padding: theme.spacing(1),
    overflow: 'auto',
  },
  flowchartPane: {
    height: '100%',
    backgroundColor: '#202020',
    padding: theme.spacing(1),
    overflow: 'auto',
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
  ['600px', 100],
  [1, 1],
];
let selectedInstance = '';
let selectedModule = '';
let selectedPipeline = 0;

function Status({ root }) {
  const classes = useStyles();

  const [currentInstance, setCurrentInstance] = React.useState(selectedInstance);
  const [currentModule, setCurrentModule] = React.useState(selectedModule);
  const [currentPipeline, setCurrentPipeline] = React.useState(selectedPipeline);

  const queryLocalStatus = useQuery(
    'status',
    async () => {
      const res = await fetch('/api/v1/status');
      if (res.status === 200) {
        return await res.json();
      } else {
        return null;
      }
    }
  );

  const queryRemoteStatus = useQuery(
    `status:${root}`,
    async () => {
      if (root === '/') return {};
      const res = await fetch(`/api/v1/repo${root}`);
      if (res.status === 200) {
        const data = await res.json();
        return data.instances;
      } else {
        return null;
      }
    }
  );

  const selectInstance = (inst) => {
    selectedInstance = inst;
    setCurrentInstance(inst);
  }

  const selectPipeline = (mod, i) => {
    selectedModule = mod;
    selectedPipeline = i;
    setCurrentModule(mod);
    setCurrentPipeline(i);
  }

  const instanceMap = { '': queryLocalStatus.data };
  if (queryRemoteStatus.data) Object.assign(instanceMap, queryRemoteStatus.data);

  const moduleMap = instanceMap[currentInstance]?.modules || {};
  const instanceList = Object.entries(instanceMap);
  const moduleList = Object.entries(moduleMap);

  const now = Date.now();

  return (
    <div className={classes.root}>

      {/* Toolbar */}
      <Toolbar/>

      {/* Main View */}
      <div className={classes.main}>
        <SplitPane split="vertical" onChange={pos => splitPos[0] = pos}>

          <SplitPane initialSize={splitPos[0][0]} onChange={pos => splitPos[1] = pos}>

            {/* Instance List */}
            <Pane initialSize={splitPos[1][0]} className={classes.instanceListPane}>
              <List dense disablePadding>
                {instanceList.map(
                  ([id, instance]) => (
                    <ListItem
                      key={id}
                      button
                      selected={currentInstance === id}
                      onClick={() => selectInstance(id)}
                    >
                      <ListItemIcon className={classes.listIcon}><LocalHostIcon/></ListItemIcon>
                      <ListItemText
                        primary={id ? 'Remote Instance' : 'Local Host'}
                        secondary={
                          id && (
                            <div>
                              <Typography component="p" variant="caption" noWrap>{id}</Typography>
                              {now - instance.timestamp > 10000 ? (
                                <Typography component="p" variant="caption" noWrap>
                                  {'Inactive since '}
                                  <TimeAgo date={instance.timestamp}/>
                                </Typography>
                              ) : (
                                <Typography
                                  component="p"
                                  variant="caption"
                                  noWrap
                                  className={classes.instanceActive}
                                >
                                  Active
                                </Typography>
                              )}
                            </div>
                          )
                        }
                      />
                    </ListItem>
                  )
                )}
              </List>
            </Pane>

            {/* Pipeline List */}
            <Pane initialSize={splitPos[1][1]} className={classes.pipelineListPane}>
              {moduleList.length === 0 ? (
                <Nothing text="No running pipelines"/>
              ) : (
                <List dense disablePadding>
                  {moduleList.map(([name, { graph }]) => (
                    <React.Fragment key={name}>
                      <ListItem disableGutters disabled>
                        <ListItemIcon className={classes.listIcon}><ModuleIcon/></ListItemIcon>
                        <ListItemText primary={name}/>
                      </ListItem>
                      <List dense disablePadding className={classes.pipelineList}>
                        {graph.roots.map(i => (
                          <ListItem
                            key={i}
                            button
                            selected={name === currentModule && i === currentPipeline}
                            onClick={() => selectPipeline(name, i)}
                          >
                            <ListItemIcon className={classes.listIcon}><PipelineIcon/></ListItemIcon>
                            <ListItemText primary={graph.nodes[i].name}/>
                          </ListItem>
                        ))}
                      </List>
                    </React.Fragment>
                  ))}
                </List>
              )}
            </Pane>

          </SplitPane>

          {/* Flowchart */}
          <Pane initialSize={splitPos[0][1]} className={classes.flowchartPane}>
            {moduleList.length === 0 ? (
              <Nothing text="No running pipelines"/>
            ) : (
              (moduleMap[currentModule] && (
                <Flowchart nodes={moduleMap[currentModule].graph.nodes} root={currentPipeline}/>
              )) || (
                <Nothing text="No pipeline selected"/>
              )
            )}
          </Pane>

        </SplitPane>
      </div>
    </div>
  );
}

export default Status;