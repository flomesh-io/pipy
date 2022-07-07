import React from 'react';

import { makeStyles } from '@material-ui/core/styles';

const useStyles = makeStyles(theme => ({
  main: {
    height: '100%',
    backgroundColor: '#202020',
    paddingLeft: 20,
    overflow: 'auto',
  },
  log: {
    fontFamily: 'Menlo, Courier New',
    fontSize: '11px',
    lineHeight: '12px',
  },
}));

function LogView({ uuid, name }) {
  const classes = useStyles();
  const logEl = React.useRef();

  React.useEffect(
    () => {
      const text = document.createTextNode('');
      const pre = logEl.current;
      while (pre.firstChild) pre.removeChild(pre.firstChild);
      pre.appendChild(text);
      const loc = window.location;
      // const url = `ws://localhost:6060/api/v1/log/${uuid}/${name}`; // For development mode
      const url = `ws://${loc.host}/api/v1/log/${uuid}/${name}`;
      const ws = new WebSocket(url);
      ws.addEventListener('open', () => { text.data = ''; ws.send('watch\n'); });
      ws.addEventListener('message', evt => text.appendData(evt.data));
      return () => ws.close();
    },
    [uuid, name]
  );

  return (
    <div className={classes.main}>
      <pre ref={logEl} className={classes.log}/>
    </div>
  );
}

export default LogView;
