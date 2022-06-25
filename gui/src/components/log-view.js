import React from 'react';

import { makeStyles } from '@material-ui/core/styles';

const useStyles = makeStyles(theme => ({
  main: {
    height: `calc(100% - ${theme.TOOLBAR_HEIGHT}px)`,
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
      logEl.current.appendChild(text);
      const loc = window.location;
      // const url = `ws://localhost:6060/api/v1/log/${uuid}/${name}`; // For development mode
      const url = `ws://${loc.host}/api/v1/log/${uuid}/${name}`;
      const ws = new WebSocket(url);
      ws.addEventListener('open', () => ws.send('watch\n'));
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
