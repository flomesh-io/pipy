import React from 'react';

import { makeStyles } from '@material-ui/core/styles';

// Material-UI components
import List from '@material-ui/core/List';
import ListItem from '@material-ui/core/ListItem';
import ListItemIcon from '@material-ui/core/ListItemIcon';
import ListItemText from '@material-ui/core/ListItemText';

// Components
import Flowchart from './flowchart';
import Instances, { InstanceContext } from './instances';
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
  instanceListPane: {
    height: '100%',
    backgroundColor: '#282828',
    padding: theme.spacing(1),
    overflow: 'auto',
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

function Status({ root }) {
  const classes = useStyles();
  const instanceContext = React.useContext(InstanceContext);

  const [currentModule, setCurrentModule] = React.useState('');
  const [currentPipeline, setCurrentPipeline] = React.useState(0);

  const selectPipeline = (mod, i) => {
    setCurrentModule(mod);
    setCurrentPipeline(i);
  }

  const moduleMap = instanceContext.currentInstance?.status?.modules || {};
  const moduleList = Object.entries(moduleMap);

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
              <Instances root={root}/>
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
