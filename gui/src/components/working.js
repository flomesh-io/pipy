import React from 'react';

import { makeStyles } from '@material-ui/core/styles';

import Backdrop from '@material-ui/core/Backdrop';
import CircularProgress from '@material-ui/core/CircularProgress';
import Typography from '@material-ui/core/Typography';

const useStyles = makeStyles(() => ({
  backdrop: {
  },
}));

function Working({ open, text }) {
  const classes = useStyles();
  return (
    <Backdrop className={classes.backdrop} open={open}>
      <CircularProgress/>
      <Typography variant="h6" color="textSecondary">
        {text}
      </Typography>
    </Backdrop>
  );
}

export default Working;