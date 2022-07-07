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

const MAX_LOG_VIEW_DATA_SIZE = 256 * 1024;

export function useLogWatcher(uuid, name, cb) {
  React.useEffect(
    () => {
      let ws, is_reconnecting = false;
      const txt = document.createTextNode('');
      const loc = window.location;
      // const url = `ws://localhost:6060/api/v1/log/${uuid}/${name}`; // For development mode
      const url = `ws://${loc.host}/api/v1/log/${uuid}/${name}`;
      const append = str => {
        const n = txt.length;
        const m = str.length + n;
        if (m > MAX_LOG_VIEW_DATA_SIZE) {
          let excess = m - MAX_LOG_VIEW_DATA_SIZE;
          if (excess > n) {
            txt.data = '';
            txt.appendData(str);
            txt.deleteData(0, excess - n);
          } else {
            txt.deleteData(0, excess);
            txt.appendData(str);
          }
        } else {
          txt.appendData(str);
        }
        const p = txt.parentNode;
        if (p) p.scrollIntoView(false);
      };
      const connect = () => {
        const reconnect = () => {
          if (is_reconnecting) return;
          ws.close();
          setTimeout(() => { is_reconnecting = false; connect(); }, 5000);
          is_reconnecting = true;
        }
        ws = new WebSocket(url);
        ws.addEventListener('open', () => { txt.data = ''; ws.send('watch\n'); });
        ws.addEventListener('message', evt => append(evt.data));
        ws.addEventListener('close', reconnect);
        ws.addEventListener('error', reconnect);
      };
      connect();
      cb(txt);
      return () => ws.close();
    },
    [uuid, name]
  );
}

function LogView({ uuid, name }) {
  const classes = useStyles();
  const logEl = React.useRef();

  useLogWatcher(
    uuid, name,
    text => {
      const pre = logEl.current;
      while (pre.firstChild) pre.removeChild(pre.firstChild);
      pre.appendChild(text);
    }
  );

  return (
    <div className={classes.main}>
      <pre ref={logEl} className={classes.log}/>
    </div>
  );
}

export default LogView;
