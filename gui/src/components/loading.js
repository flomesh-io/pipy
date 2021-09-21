import React from 'react';

import { makeStyles } from '@material-ui/core/styles';

import CircularProgress from '@material-ui/core/CircularProgress';

const useStyles = makeStyles(() => ({
  nothing: {
    width: '100%',
    height: '100%',
    alignItems: 'center',
    display: 'flex',
    flexDirection: 'column',
    justifyContent: 'center',
  },
}));

function Loading() {
  const classes = useStyles();
  return (
    <div className={classes.nothing}>
      <CircularProgress/>
    </div>
  );
}

export default Loading;