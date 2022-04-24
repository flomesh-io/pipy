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
  chart: {
    width: '100%',
  },
  chartRow: {
    backgroundColor: '#1c1c1c',
  },
  chartRowTitle: {
  },
  chartRowNumber: {
    width: '100px',
  },
  chartRowSparkline: {
    width: '50%',
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
                    <ListItem
                      key={name}
                      selected={currentMetric === name}
                      disableGutters
                      button
                      onClick={() => setCurrentMetric(name)}
                    >
                      <div className={classes.listItem}>
                        <MetricItem title={name} values={values}/>
                      </div>
                    </ListItem>
                  ))}
                </List>
              )}
            </Pane>

          </SplitPane>

          {/* Metric Chart */}
          <Pane initialSize={splitPos[0][1]} className={classes.metricChartPane}>
            {currentMetric ? (
              <Chart path={root} uuid={instance?.id ? uuid : ''} title={currentMetric}/>
            ) : (
              <Nothing text="No metric selected"/>
            )}
          </Pane>

        </SplitPane>
      </div>
    </div>
  );
}

function MetricItem({ title, values }) {
  const classes = useStyles();
  const [cursorX, setCursorX] = React.useState(-1);

  const handleCursorMove = (x) => {
    setCursorX(x);
  }

  const { min, max } = React.useMemo(
    () => {
      let min = Number.POSITIVE_INFINITY;
      let max = Number.NEGATIVE_INFINITY;
    
      values.forEach(
        v => {
          const y = -(v || 0);
          if (y < min) min = y;
          if (y > max) max = y;  
        }
      );
    
      if (max < 0) max = 0;
      if (max - min < 100) min = max - 100;

      return { min, max };
    },
    [values]
  );

  const i = 60 - cursorX;
  const n = values.length;
  const y = cursorX < 0 ? values[n-1] : (values[n-i] || 0);

  return (
    <div>
      <Typography color="textSecondary">{title}</Typography>
      <Sparkline
        values={values}
        min={min}
        max={max}
        height={50}
        cursorX={cursorX}
        onCursorMove={handleCursorMove}
      />
      <div className={classes.pointNumber}>
        {y}
      </div>
    </div>
  );
}

function Chart({ path, uuid, title }) {
  const classes = useStyles();
  const [cursorX, setCursorX] = React.useState(-1);

  const queryMetric = useQuery(
    `metrics:${path}:${uuid}:${title}`,
    async () => {
      if (uuid) {
        const res = await fetch(`/api/v1/metrics/${uuid}/${title}`);
        if (res.status === 200) {
          const data = await res.json();
          return data.metrics;
        }
      } else {
        const res = await fetch(`/api/v1/metrics/${title}`);
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

  const { list, min, max } = React.useMemo(
    () => {
      const root = queryMetric.data?.[0];
      const list = [];

      const traverse = (k, m) => {
        if (m.s instanceof Array) {
          if (k.length > 0) k += '/';
          m.s.forEach(
            m => traverse(k + m.k, m)
          );
        } else {
          list.push({ key: k, values: m.v });
        }
      }

      if (root) {
        traverse('', root);
      }

      let min = Number.POSITIVE_INFINITY;
      let max = Number.NEGATIVE_INFINITY;

      list.forEach(
        ({values}) => (
          values.forEach(
            v => {
              const y = -(v || 0);
              if (y < min) min = y;
              if (y > max) max = y;
            }
          )
        )
      );

      if (max < 0) max = 0;
      if (max - min < 100) min = max - 100;

      return { list, min, max };
    },
    [queryMetric.data]
  );

  const handleCursorMove = (x) => {
    setCursorX(x);
  }

  return (
    <table className={classes.chart}>
      {list.map(
        ({ key, values }) => (
          <ChartRow
            title={key}
            values={values}
            min={min}
            max={max}
            cursorX={cursorX}
            onCursorMove={handleCursorMove}
          />
        )
      )}
    </table>
  );
}

function ChartRow({ title, values, min, max, cursorX, onCursorMove }) {
  const classes = useStyles();
  const i = 60 - cursorX;
  const n = values.length;
  const y = cursorX < 0 ? values[n-1] : (values[n-i] || 0);
  return (
    <tr className={classes.chartRow}>
      <td className={classes.chartRowTitle}>
        <Typography>{title}</Typography>
      </td>
      <td className={classes.chartRowNumber}>
        <Typography>{y}</Typography>
      </td>
      <td className={classes.chartRowSparkline}>
        <Sparkline
          values={values}
          min={min}
          max={max}
          height={26}
          cursorX={cursorX}
          onCursorMove={onCursorMove}
        />
      </td>
    </tr>
  );
}

let counter = 0;

function Sparkline({ values, min, max, height, cursorX, onCursorMove }) {
  const classes = useStyles();
  const canvasEl = React.useRef(null);
  const cursorEl = React.useRef(null);
  const circleEl = React.useRef(null);

  const margin = (max - min) * 0.1;
  min -= margin;
  max += margin;

  const { edge, area } = React.useMemo(
    () => {
      console.log(counter++);

      const line = [];
      for (let i = 1; i <= 60; i++) {
        const x = 60 - i;
        const y = -(values[values.length - i] || 0);
        line.push(i > 1 ? `L ${x},${y}` : `M ${x},${y}`);
      }
      const edge = line.join(' ');
      const area = edge + `L 0,${max-margin} L 59,${max-margin} z`;
      return { edge, area };
    },
    [values, min, max]
  );
  
  const handleMouseMove = e => {
    const rect = canvasEl.current.getBoundingClientRect();
    const d = rect.x + rect.width - e.pageX;
    const i = Math.max(0, Math.min(59, (d * 59 / rect.width) | 0));
    const x = 59 - i;
    onCursorMove(x);
  }

  const x = cursorX;
  const i = 59 - x;
  const n = values.length;
  const v = values[n - 1 - i] || 0;
  const y = -v;

  return (
    <svg
      ref={canvasEl}
      viewBox={`0 ${min} 60 ${max - min}`}
      preserveAspectRatio="none"
      className={classes.sparkline}
      style={{ height }}
      onMouseMove={handleMouseMove}
      onMouseLeave={() => onCursorMove(-1)}
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
        x1={x}
        x2={x}
        y1={min}
        y2={max}
        vectorEffect="non-scaling-stroke"
        stroke={cursorX >= 0 ? 'green' : 'none'}
        strokeWidth="1"
        strokeLinejoin="round"
      />
      <line
        ref={circleEl}
        vectorEffect="non-scaling-stroke"
        x1={x}
        y1={y}
        x2={x}
        y2={y}
        stroke={cursorX >= 0 ? 'white' : 'none'}
        strokeWidth="5"
        strokeLinecap="round"
      />
    </svg>
  );
}

export default Metrics;
