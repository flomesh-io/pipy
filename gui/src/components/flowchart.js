import React from 'react';

import { makeStyles } from '@material-ui/core/styles';

const useStyles = makeStyles(theme => ({
  flowchartContainer: {
    position: 'relative',
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'flex-start',
  },
  boxChunk: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'stretch',
    marginBottom: 38,
  },
  boxPipeline: {
    background: 'orange',
    color: 'black',
    height: 18,
    lineHeight: '18px',
    marginTop: 1,
    minWidth: 150,
    paddingLeft: 8,
    paddingRight: 8,
    fontWeight: 'bold',
  },
  boxFilter: {
    background: '#fff',
    color: 'black',
    height: 18,
    lineHeight: '18px',
    marginTop: 1,
    minWidth: 150,
    paddingLeft: 8,
    paddingRight: 8,
  },
  boxEmpty: {
    height: 18,
    lineHeight: '18px',
    marginTop: 1,
    minWidth: 150,
    paddingLeft: 8,
    paddingRight: 8,
  },
  boxInput: {
    borderTopLeftRadius: 6,
    borderTopRightRadius: 6,
  },
  boxOutput: {
    borderBottomLeftRadius: 6,
    borderBottomRightRadius: 6,
  },
}));

function Flowchart({ nodes, root }) {
  const classes = useStyles();

  const graphEl = React.useRef();

  React.useEffect(() => {
    const div = graphEl.current;
    if (div && nodes) {
      drawPipeline(div, nodes, root, classes);
    }
    return () => {
      if (div) {
        while (div.firstChild) {
          div.removeChild(div.firstChild);
        }
      }
    }
  }, [nodes, root, classes]);

  if (!nodes) return null;

  return <div ref={graphEl} className={classes.flowchartContainer}/>;
}

function drawPipeline(container, graph, root, classes) {
  if (!graph[root]) return;

  const boxes = [];
  const calls = {};
  const falls = {};
  const exits = {};
  const lines = [];
  const outputs = [];

  let chunk = null;
  let rightMost = 0;

  const drawGutter = () => {
    if (chunk && !chunk.firstChild) return;
    chunk = document.createElement('div');
    chunk.className = classes.boxChunk;
    container.appendChild(chunk);
  }

  const drawArrow = (x, y) => {
    const path = document.createElementNS(svgns, 'path');
    path.setAttribute(
      'd',
      `M ${x  },${y  } ` +
      `L ${x-6},${y-8} ` +
      `  ${x+6},${y-8} `
    );
    path.setAttribute('fill', '#fff');
    path.setAttribute('stroke', 'none');
    svg.appendChild(path);
  }

  const drawArrowRight = (x, y) => {
    const path = document.createElementNS(svgns, 'path');
    path.setAttribute(
      'd',
      `M ${x  },${y  } ` +
      `L ${x-8},${y-6} ` +
      `  ${x-8},${y+6} `
    );
    path.setAttribute('fill', '#fff');
    path.setAttribute('stroke', 'none');
    svg.appendChild(path);
  }

  const drawNode = (i, depth) => {
    const node = i === '' ? null : graph[i];
    const type = node?.t;
    const outType = node?.ot;
    const isPipeline = (type === 'root' || type === 'pipeline');
    const isJoint = (type === 'joint');

    let isInput = isPipeline || Boolean(falls[i]) || Boolean(exits[i]);
    let isOutput = isJoint;

    if (isInput) drawGutter();

    const div = document.createElement('div');
    const span = document.createElement('span');
    span.textContent = node?.name || ' ';
    div.style.marginLeft = depth * 50 + 'px';
    div.appendChild(span);
    chunk.appendChild(div);
    boxes[i] = div;
    lines.push(div);

    if (node?.name === 'output') {
      outputs.push(div);
    }

    if (exits[i]) {
      const { start } = exits[i];
      let max = rightMost;
      for (let i = lines.length - 1; i >= start; i--) {
        const right = lines[i].getBoundingClientRect().right;
        if (right > max) max = right;
      }
      max += 50;
      exits[i].x = rightMost = max;
    }

    if (i === '') {
      div.classList.add(classes.boxEmpty);
      return;
    }

    if (node.c) {
      if (isJoint) {
        const callees = (calls[i] = calls[i] || []);
        for (const i of node.c) callees.push(i);
        depth++;
      }

      for (const i of node.c) {
        drawNode(i, depth);
      }

      // find where the filter output goes
      if (type === 'joint' && outType !== 'subs') {
        let target = null;
        let p = node.p;
        let parent = graph[p];
        const s = parent.c;
        if (s[s.length - 1] !== i) {
          target = s[s.indexOf(i) + 1];
        } else {
          while (p !== undefined) {
            parent = graph[p];
            if (parent.ot !== 'subs') break;
            if (parent.t === 'joint') {
              const pipeline = graph[parent.p];
              const siblings = pipeline.c;
              if (siblings[siblings.length - 1] !== p) {
                target = siblings[siblings.indexOf(p) + 1];
                break;
              }
            } else if (parent.t === 'root') {
              target = '';
              break;
            }
            p = parent.p;
          }
        }
        if (target !== null) {
          const sources = falls[target] = falls[target] || [];
          sources.push(i);
        }
      }
    }

    if (type === 'filter') {
      const siblings = graph[node.p].c;
      if (siblings[siblings.length - 1] === i) isOutput = true;

    } else if (type === 'pipeline') {
      if (!node.c) isOutput = true;
    }

    // find out where the end of pipeline returns to
    if (isOutput && !isJoint) {
      let target = null;
      let p = node.p;
      while (p !== undefined) {
        const parent = graph[p];
        if (parent.t === 'joint') {
          if (parent.ot !== 'subs') break;
          const pipeline = graph[parent.p];
          const siblings = pipeline.c;
          if (siblings[siblings.length - 1] !== p) {
            target = siblings[siblings.indexOf(p) + 1];
          }
        } else if (parent.t === 'root') {
          target = '';
        }
        if (target !== null) {
          const exit = (
            exits[target] = exits[target] || {
              start: lines.length,
              sources: [],
            }
          );
          exit.sources.push(i);
          break;
        }
        p = parent.p;
      }
    }

    if (isOutput) drawGutter();

    div.classList.add(isPipeline ? classes.boxPipeline : classes.boxFilter);
    if (isInput) div.classList.add(classes.boxInput);
    if (isOutput) div.classList.add(classes.boxOutput);
  }

  drawGutter();
  drawNode(root, 1);
  drawNode('', 1); // empty ending node

  for (const div of outputs) {
    const box = div.getBoundingClientRect();
    const right = box.right + 32;
    if (right > rightMost) rightMost = right;
  }

  const svgns = 'http://www.w3.org/2000/svg';
  let bounds = container.getBoundingClientRect();
  const x0 = bounds.left;
  const y0 = bounds.top;
  container.style.minWidth = (rightMost - x0) + 'px';
  bounds = container.getBoundingClientRect();
  const svg = document.createElementNS(svgns, 'svg');
  svg.setAttribute('width', bounds.width);
  svg.setAttribute('height', bounds.height);
  svg.style.position = 'absolute';
  svg.style.top = 0;
  svg.style.left = 0;
  svg.style.pointerEvents = 'none';
  container.appendChild(svg);

  for (const div of outputs) {
    const box = div.getBoundingClientRect();
    const x = box.right - x0;
    const y = 0.5 * (box.top + box.bottom) - y0;
    const path = document.createElementNS(svgns, 'path');
    path.setAttribute(
      'd',
      `M ${x   },${y} `+
      `L ${x+26},${y} `
    );
    path.setAttribute('fill', 'none');
    path.setAttribute('stroke', '#fff');
    path.setAttribute('stroke-width', '3');
    path.setAttribute('stroke-dasharray', '3,3');
    svg.appendChild(path);
    drawArrowRight(x + 32, y);
  }

  for (const src in calls) {
    const box = boxes[src].getBoundingClientRect();
    const x1 = box.left + 30 - x0;
    const y1 = box.bottom + 2 - y0;
    for (const dst of calls[src]) {
      const box = boxes[dst].getBoundingClientRect();
      const x2 = box.left + 30 - x0;
      const y2 = box.top - 2 - y0;
      const path = document.createElementNS(svgns, 'path');
      path.setAttribute(
        'd',
        `M ${x1  },${y1   } `+
        `L ${x1  },${y2-25}` +
        `  ${x1+5},${y2-20}` +
        `  ${x2-5},${y2-20}` +
        `  ${x2  },${y2-15}` +
        `  ${x2  },${y2-5 }`
      );
      path.setAttribute('fill', 'none');
      path.setAttribute('stroke', '#fff');
      path.setAttribute('stroke-width', '3');
      svg.appendChild(path);
      drawArrow(x2, y2);
    }
  }

  for (const dst in falls) {
    const box = boxes[dst].getBoundingClientRect();
    const x2 = box.left + 30 - x0;
    const y2 = box.top - 2 - y0;
    for (const src of falls[dst]) {
      const box = boxes[src].getBoundingClientRect();
      const x1 = box.left + 30 - x0;
      const y1 = box.bottom + 2 - y0;
      const path = document.createElementNS(svgns, 'path');
      if (x1 - x2 < 10) {
        path.setAttribute(
          'd',
          `M ${x1},${y1  } `+
          `L ${x1},${y2-5} `
        );
      } else {
        path.setAttribute(
          'd',
          `M ${x1  },${y1   } `+
          `L ${x1  },${y1+10}` +
          `  ${x1-5},${y1+15}` +
          `  ${x2+5},${y1+15}` +
          `  ${x2  },${y1+20}` +
          `  ${x2  },${y2-5 }`
        );
      }
      path.setAttribute('fill', 'none');
      path.setAttribute('stroke', '#fff');
      path.setAttribute('stroke-width', '3');
      if (graph[src].ot === 'others') {
        path.setAttribute('stroke-dasharray', '3,3');
        const path2 = document.createElementNS(svgns, 'path');
        const path3 = document.createElementNS(svgns, 'path');
        path2.setAttribute(
          'd',
          `M ${x2   },${y2   } `+
          `L ${x2   },${y2-15}` +
          `  ${x2+5 },${y2-20}` +
          `  ${x2+15},${y2-20}`
        );
        path3.setAttribute(
          'd',
          `M ${x2+15},${y2-20}` +
          `L ${x2+36},${y2-20}`
        )
        path2.setAttribute('fill', 'none');
        path2.setAttribute('stroke', '#fff');
        path2.setAttribute('stroke-width', '3');
        path3.setAttribute('fill', 'none');
        path3.setAttribute('stroke', '#fff');
        path3.setAttribute('stroke-width', '3');
        path3.setAttribute('stroke-dasharray', '3,3');
        svg.appendChild(path2);
        svg.appendChild(path3);
      }
      svg.appendChild(path);
    }
    drawArrow(x2, y2);
  }

  for (const dst in exits) {
    const box = boxes[dst].getBoundingClientRect();
    const x2 = box.left + 30 - x0;
    const y2 = box.top - 2 - y0;
    const x3 = exits[dst].x - x0;
    for (const src of exits[dst].sources) {
      const box = boxes[src].getBoundingClientRect();
      const x1 = box.left + 80 - x0;
      const y1 = box.bottom + 2 - y0;
      const path = document.createElementNS(svgns, 'path');
      if (y1 + 20 >= y2 - 25) {
        if (x1 <= x2 + 50) {
          path.setAttribute(
            'd',
            `M ${x2},${y1  } `+
            `L ${x2},${y2-5} `
          );
        } else {
          path.setAttribute(
            'd',
            `M ${x1  },${y1   } `+
            `L ${x1  },${y1+10}` +
            `  ${x1-5},${y1+15}` +
            `  ${x2+5},${y1+15}` +
            `  ${x2  },${y1+20}` +
            `  ${x2  },${y2-5 }`
          );
        }
      } else {
        path.setAttribute(
          'd',
          `M ${x1  },${y1   } `+
          `L ${x1  },${y1+10}` +
          `  ${x1+5},${y1+15}` +
          `  ${x3-5},${y1+15}` +
          `  ${x3  },${y1+20}` +
          `  ${x3  },${y2-25}` +
          `  ${x3-5},${y2-20}` +
          `  ${x2+5},${y2-20}` +
          `  ${x2  },${y2-15}` +
          `  ${x2  },${y2-5 }`
        );  
      }
      path.setAttribute('fill', 'none');
      path.setAttribute('stroke', '#fff');
      path.setAttribute('stroke-width', '3');
      svg.appendChild(path);
    }
    drawArrow(x2, y2);
  }
}

export default Flowchart;
