import React from 'react';

import { makeStyles } from '@material-ui/core/styles';

const useStyles = makeStyles(theme => ({
  diagram: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    padding: theme.spacing(1),
  },
}));

const Box = ({ x, y, width, height, text, fill }) => {
  return (
    <React.Fragment>
      <rect
        x={x}
        y={y}
        width={width}
        height={height}
        rx="6"
        fill={fill}
      />
      <text
        x={x + width/2}
        y={y + height/2 + 5}
        fontSize="16px"
        fontWeight="bold"
        textAnchor="middle"
        fill="#000"
        opacity="65%"
      >
        {text}
      </text>
    </React.Fragment>
  );
}

const ArrowR = ({ x, y }) => {
  return (
    <path
      d={
        `M ${x  },${y  } ` +
        `L ${x-8},${y-6} ` +
        `  ${x-8},${y+6} `
      }
      fill="#fff"
      stroke="none"
    />
  );
}

const ArrowD = ({ x, y }) => {
  return (
    <path
      d={
        `M ${x  },${y  } ` +
        `L ${x-6},${y-8} ` +
        `  ${x+6},${y-8} `
      }
      fill="#fff"
      stroke="none"
    />
  );
}

const ArrowU = ({ x, y }) => {
  return (
    <path
      d={
        `M ${x  },${y  } ` +
        `L ${x-6},${y+8} ` +
        `  ${x+6},${y+8} `
      }
      fill="#fff"
      stroke="none"
    />
  );
}

const Line = ({ x1, x2, y, text }) => {
  if (text === undefined) return null;
  return (
    <React.Fragment>
      <path
        d={`M ${x1+2},${y} L ${x2-5},${y}`}
        fill="none"
        stroke="#fff"
        strokeWidth="3"
      />
      <ArrowR x={x2-2} y={y}/>
      <text
        x={(x1+x2)/2}
        y={y-10}
        fontSize="15px"
        textAnchor="middle"
        fill="#fff"
        opacity="65%"
      >
        {text}
      </text>
    </React.Fragment>
  );
}

const LineSubInput = ({ x1, y1, x2, y2, text }) => {
  const d = (y2 > y1 ? 1 : -1);
  if (text === undefined) return null;
  return (
    <React.Fragment>
      <path
        d={
          `M ${x1  },${y1+d*2} `+
          `L ${x1  },${y2-d*5}` +
          `  ${x1+5},${y2}` +
          `  ${x2-5},${y2}`
        }
        fill="none"
        stroke="#fff"
        strokeWidth="3"
      />
      <ArrowR x={x2-2} y={y2}/>
      {text && (
        <text
          x={x1-10}
          y={(y1+y2)/2+5}
          fontSize="15px"
          textAnchor="end"
          fill="#fff"
          opacity="65%"
        >
          {text}
        </text>
      )}
    </React.Fragment>
  );
}

const LineSubOutput = ({ x1, y1, x2, y2, text }) => {
  const c = (y2 < y1 ? 5 : -5);
  if (text === undefined) return null;
  return (
    <React.Fragment>
      <path
        d={
          `M ${x1+2},${y1  } `+
          `L ${x2-5},${y1  }` +
          `  ${x2  },${y1-c}` +
          `  ${x2  },${y2+c}`
        }
        fill="none"
        stroke="#fff"
        strokeWidth="3"
      />
      {y2 < y1 ? (
        <ArrowU x={x2} y={y2+2}/>
      ): (
        <ArrowD x={x2} y={y2-2}/>
      )}
      {text && (
        <text
          x={x2+10}
          y={(y1+y2)/2+5}
          fontSize="15px"
          textAnchor="start"
          fill="#fff"
          opacity="65%"
        >
          {text}
        </text>
      )}
    </React.Fragment>
  );
}

const FilterDiagram = ({ name, input, output, subInput, subOutput, subType }) => {
  const classes = useStyles();
  let height = 50;
  switch (subType) {
    case 'link': height = 120; break;
    case 'demux': height = 160; break;
    case 'mux': height = 210; break;
  }
  return (
    <div className={classes.diagram}>
      <svg width="500" height={height}>
        <Box x={150} y={10} width={200} height={30} text={name} fill="orange"/>
        <Line x1={10} x2={150} y={25} text={input}/>
        <Line x1={350} x2={490} y={25} text={output}/>

        {(subType === 'link' || subType == 'demux') && (
          <React.Fragment>
            <Box x={200} y={80} width={100} height={30} text="Sub-pipeline" fill="lightgreen"/>
            <LineSubInput x1={170} y1={40} x2={200} y2={95} text={subInput}/>
            <LineSubOutput x1={300} y1={95} x2={330} y2={40} text={subOutput}/>
          </React.Fragment>
        )}

        {subType === 'demux' && (
          <React.Fragment>
            <Box x={200} y={120} width={100} height={30} text="Sub-pipeline" fill="lightgreen"/>
            <LineSubInput x1={170} y1={40} x2={200} y2={135} text=""/>
            <LineSubOutput x1={300} y1={135} x2={330} y2={40} text={subOutput && ""}/>
          </React.Fragment>
        )}

        {subType === 'mux' && (
          <React.Fragment>
            <Box x={200} y={80} width={100} height={50} text="Sub-pipeline" fill="lightgreen"/>
            <LineSubInput x1={170} y1={40} x2={200} y2={95} text={subInput}/>
            <LineSubOutput x1={300} y1={95} x2={330} y2={40} text={subOutput}/>
            <Box x={150} y={170} width={200} height={30} text={name} fill="orange"/>
            <Line x1={10} x2={150} y={185} text={input}/>
            <Line x1={350} x2={490} y={185} text={output}/>
            <LineSubInput x1={170} y1={170} x2={200} y2={115} text={subInput}/>
            <LineSubOutput x1={300} y1={115} x2={330} y2={170} text={subOutput}/>
          </React.Fragment>
        )}

      </svg>
    </div>
  );
}

export default FilterDiagram;
