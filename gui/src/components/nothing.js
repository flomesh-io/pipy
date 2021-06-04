import React from 'react';

import { makeStyles } from '@material-ui/core/styles';

import Typography from '@material-ui/core/Typography';

const useStyles = makeStyles(() => ({
  nothing: {
    width: '100%',
    height: '100%',
    textAlign: 'center',
    display: 'flex',
    flexDirection: 'column',
    justifyContent: 'center',
  },
}));

function Nothing({ text }) {
  const classes = useStyles();
  return (
    <div className={classes.nothing}>
      <Typography variant="h6" color="textSecondary">
        {text}
      </Typography>
    </div>
  );
}

export default Nothing;