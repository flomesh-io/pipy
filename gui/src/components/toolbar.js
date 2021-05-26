import React from 'react';

import { makeStyles } from '@material-ui/core/styles';

// Material-UI components
import Grid from '@material-ui/core/Grid';
import IconButton from '@material-ui/core/IconButton';

// CSS styles
const useStyles = makeStyles(theme => ({
  toolbar: {
    height: theme.TOOLBAR_HEIGHT,
    flexBasis: theme.TOOLBAR_HEIGHT,
    flexGrow: 0,
    flexShrink: 0,
    paddingLeft: theme.spacing(1),
  },
  gap: {
    flexBasis: theme.spacing(3),
    flexGrow: 0,
    flexShrink: 0,
  },
  filling: {
    flexGrow: 1,
  }
}));

function Toolbar({ children }) {
  const classes = useStyles();
  return (
    <Grid
      item
      container
      wrap="nowrap"
      alignItems="center"
      className={classes.toolbar}
    >
      {children}
    </Grid>
  );
}

export function ToolbarButton({ disabled, onClick, children }) {
  return (
    <Grid item>
      <IconButton
        size="small"
        disabled={disabled}
        onClick={onClick}
      >
        {children}
      </IconButton>
    </Grid>
  );
}

export function ToolbarGap() {
  const classes = useStyles();
  return (
    <Grid item className={classes.gap}/>
  );
}

export function ToolbarFilling() {
  const classes = useStyles();
  return (
    <Grid item className={classes.filling}/>
  );
}

export default Toolbar;