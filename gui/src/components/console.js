import React from 'react';

import { makeStyles } from '@material-ui/core/styles';

// Components
import Toolbar, { ToolbarButton, ToolbarGap, ToolbarStretch } from './toolbar';

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

const Context = React.createContext({
  textNode: null,
  clearLog: () => {},
});

function Console({ onClose }) {
  const classes = useStyles();
  const context = React.useContext(Context);
  const consoleEl = React.useRef();

  React.useEffect(
    () => {
      const node = context.textNode;
      if (node) {
        const container = consoleEl.current;
        container.appendChild(node);
        container.scrollIntoView(false)
        return () => container.removeChild(node);
      }
    },
    [context.textNode]
  );

  const handleClickClear = () => {
    context.clearLog();
  }

  const handleClickClose = () => {
    onClose?.();
  }

  return (
    <div className={classes.root}>

      {/* Toolbar */}
      <Toolbar>
        <ToolbarStretch/>
        <ToolbarButton onClick={handleClickClear}>
          <ClearIcon fontSize="small"/>
        </ToolbarButton>
        {onClose && (
          <ToolbarButton onClick={handleClickClose}>
            <CloseIcon fontSize="small"/>
          </ToolbarButton>
        )}
        <ToolbarGap/>
      </Toolbar>

      {/* Log View */}
      <div className={classes.main}>
        <pre ref={consoleEl} className={classes.log}/>
      </div>
    </div>
  );
}

Console.Context = Context;

export default Console;