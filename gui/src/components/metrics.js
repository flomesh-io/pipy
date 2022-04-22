import React from 'react';

import { makeStyles } from '@material-ui/core/styles';
import { useQuery } from 'react-query';

// Material-UI components
import List from '@material-ui/core/List';
import ListItem from '@material-ui/core/ListItem';
import ListItemIcon from '@material-ui/core/ListItemIcon';
import ListItemText from '@material-ui/core/ListItemText';

// Components
import Instances, { InstanceContext } from './instances';
import Nothing from './nothing';
import Pane from 'react-split-pane/lib/Pane';
import SplitPane from 'react-split-pane';
import Toolbar from './toolbar';

// Icons
import ModuleIcon from '@material-ui/icons/DescriptionSharp';

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
  metricListPane: {
    height: '100%',
    backgroundColor: '#252525',
    padding: theme.spacing(1),
    overflow: 'auto',
  },
  metricChartPane: {
    height: '100%',
    backgroundColor: '#202020',
    padding: theme.spacing(1),
    overflow: 'auto',
  },
  listIcon: {
    minWidth: 36,
  },
}));

// Global state
const splitPos = [
  ['600px', 100],
  [1, 1],
];

function Metrics({ root }) {
  const classes = useStyles();
  const instanceContext = React.useContext(InstanceContext);
  const instance = instanceContext.currentInstance;
  const uuid = instance?.uuid || '';

  const [currentMetric, setCurrentMetric] = React.useState('');

  const queryMetricList = useQuery(
    `metrics:${root}:${uuid}`,
    async () => {
      if (instance?.id) {
        const res = await fetch(`/api/v1/metrics/${uuid}/`);
        if (res.status === 200) {
          const data = await res.json();
          return data.metrics;
        }
      } else {
        const res = await fetch(`/api/v1/metrics/`);
        if (res.status === 200) {
          const data = await res.json();
          return data.metrics;
        }
      }
      return null;
    }
  );

  console.log(queryMetricList.data);

  const metricList = queryMetricList.data instanceof Array ? queryMetricList.data : [];

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

            {/* Metric List */}
            <Pane initialSize={splitPos[1][1]} className={classes.metricListPane}>
              {metricList.length === 0 ? (
                <Nothing text="No metrics"/>
              ) : (
                <List dense disablePadding>
                  {metricList.map(({ k: name, v: values }) => (
                    <React.Fragment key={name}>
                      <ListItem disableGutters disabled>
                        <ListItemIcon className={classes.listIcon}><ModuleIcon/></ListItemIcon>
                        <ListItemText primary={name}/>
                      </ListItem>
                    </React.Fragment>
                  ))}
                </List>
              )}
            </Pane>

          </SplitPane>

          {/* Metric Chart */}
          <Pane initialSize={splitPos[0][1]} className={classes.metricChartPane}>
            <Nothing text="No metric selected"/>
          </Pane>

        </SplitPane>
      </div>
    </div>
  );
}

export default Metrics;
