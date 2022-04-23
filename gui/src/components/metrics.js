import React from 'react';

import { makeStyles } from '@material-ui/core/styles';
import { useQuery } from 'react-query';

// Material-UI components
import List from '@material-ui/core/List';
import ListItem from '@material-ui/core/ListItem';
import ListItemText from '@material-ui/core/ListItemText';
import Typography from '@material-ui/core/Typography';

// Components
import Instances, { InstanceContext } from './instances';
import Nothing from './nothing';
import Pane from 'react-split-pane/lib/Pane';
import SplitPane from 'react-split-pane';
import Toolbar from './toolbar';

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
  listItem: {
    width: '100%',
  },
  listIcon: {
    minWidth: 36,
  },
  sparkline: {
    width: '100%',
    height: '50px',
  },
  pointNumber: {
    position: 'relative',
    top: '-38px',
    height: 0,
    fontFamily: 'Verdana,Arial',
    fontSize: '20px',
    fontWeight: 'bolder',
    pointerEvents: 'none',
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
    },
    {
      refetchInterval: 1000,
    }
  );

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
                    <ListItem key={name} disableGutters button>
                      <div className={classes.listItem}>
                        <Sparkline title={name} values={values}/>
                      </div>
                    </ListItem>
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

function Sparkline({ title, values }) {
  const classes = useStyles();
  const canvasEl = React.useRef(null);
  const numberEl = React.useRef(null);
  const cursorEl = React.useRef(null);
  const circleEl = React.useRef(null);

  const resetNumber = () => {
    const n = values.length;
    const v = values[n - 1] || 0;
    numberEl.current.innerText = v;
    cursorEl.current.setAttribute('stroke', 'none');
    circleEl.current.setAttribute('stroke', 'none');
  }

  React.useEffect(
    resetNumber,
    [values, numberEl]
  );

  const line = [];
  let min = Number.POSITIVE_INFINITY;
  let max = Number.NEGATIVE_INFINITY;

  for (let i = 1; i <= 60; i++) {
    const x = 60 - i;
    const y = -(values[values.length - i] || 0);
    if (y < min) min = y;
    if (y > max) max = y;
    line.push(i > 1 ? `L ${x},${y}` : `M ${x},${y}`);
  }

  if (max < 0) max = 0;
  if (max - min < 100) min = max - 100;

  const margin = (max - min) * 0.1;
  min -= margin;
  max += margin;

  const handleMouseMove = e => {
    const rect = canvasEl.current.getBoundingClientRect();
    const d = rect.x + rect.width - e.pageX;
    const i = Math.max(0, Math.min(59, (d * 59 / rect.width) | 0));
    const x = 59 - i;
    const n = values.length;
    const v = values[n - 1 - i] || 0;
    const y = -v;
    const cursor = cursorEl.current;
    const circle = circleEl.current;
    cursor.setAttribute('stroke', 'green');
    cursor.setAttribute('x1', x);
    cursor.setAttribute('x2', x);
    circle.setAttribute('stroke', 'green');
    circle.setAttribute('x1', x);
    circle.setAttribute('x2', x);
    circle.setAttribute('y1', y);
    circle.setAttribute('y2', y);
    numberEl.current.innerText = v;
  }

  const edge = line.join(' ');
  const area = edge + `L 0,${max-margin} L 59,${max-margin} z`;

  return (
    <div>
      <Typography color="textSecondary">{title}</Typography>
      <svg
        ref={canvasEl}
        viewBox={`0 ${min} 60 ${max - min}`}
        preserveAspectRatio="none"
        className={classes.sparkline}
        onMouseMove={handleMouseMove}
        onMouseLeave={resetNumber}
      >
        <path
          d={area}
          fill="green"
          fillOpacity="25%"
          stroke="none"
        />
        <path
          d={edge}
          vectorEffect="non-scaling-stroke"
          fill="none"
          stroke="green"
          strokeWidth="2"
          strokeLinejoin="round"
        />
        <line
          ref={cursorEl}
          y1={min}
          y2={max}
          vectorEffect="non-scaling-stroke"
          stroke="none"
          strokeWidth="1"
          strokeLinejoin="round"
        />
        <line
          ref={circleEl}
          vectorEffect="non-scaling-stroke"
          stroke="none"
          strokeWidth="8"
          strokeLinecap="round"
        />
      </svg>
      <div ref={numberEl} className={classes.pointNumber}/>
    </div>
  );
}

export default Metrics;
