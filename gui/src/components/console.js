import React from 'react';
import GlobalState from '../global-state';

import { makeStyles } from '@material-ui/core/styles';

// Components
import Toolbar, { ToolbarButton, ToolbarFilling } from './toolbar';

// Icons
import ClearIcon from '@material-ui/icons/DeleteSweep';
import CloseIcon from '@material-ui/icons/CloseSharp';

// CSS styles
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

function Console({ onClose }) {
  const classes = useStyles();
  const globalState = React.useContext(GlobalState);
  const consoleEl = React.useRef();

  React.useEffect(
    () => {
      const node = globalState.logTextNode;
      if (node) {
        const container = consoleEl.current;
        container.appendChild(node);
        container.scrollIntoView(false)
        return () => container.removeChild(node);
      }
    },
    [globalState.logTextNode]
  );

  const handleClickClear = () => {
    globalState.clearLog();
  }

  const handleClickClose = () => {
    onClose?.();
  }

  return (
    <div className={classes.root}>

      {/* Toolbar */}
      <Toolbar>
        <ToolbarButton onClick={handleClickClear}>
          <ClearIcon fontSize="small"/>
        </ToolbarButton>
        <ToolbarFilling/>
        {onClose && (
          <ToolbarButton onClick={handleClickClose}>
            <CloseIcon fontSize="small"/>
          </ToolbarButton>
        )}
      </Toolbar>

      {/* Log View */}
      <div className={classes.main}>
        <pre ref={consoleEl} className={classes.log}/>
      </div>
    </div>
  );
}

export default Console;