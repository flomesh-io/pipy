import React from 'react';

import { makeStyles } from '@material-ui/core/styles';
import { navigate } from 'gatsby';
import { useQuery } from 'react-query';

// Material-UI components
import Box from '@material-ui/core/Box';
import Button from '@material-ui/core/Button';
import List from '@material-ui/core/List';
import ListItem from '@material-ui/core/ListItem';
import ListItemIcon from '@material-ui/core/ListItemIcon';
import ListItemText from '@material-ui/core/ListItemText';
import OutlinedInput from '@material-ui/core/OutlinedInput';

// Components
import Codebase from '../components/codebase';
import DialogNewCodebase from '../components/dialog-new-codebase';
import Loading from '../components/loading';

// Icons
import AddCircleIcon from '@material-ui/icons/AddCircle';
import ChevronRightIcon from '@material-ui/icons/ChevronRight';
import CodebaseIcon from '@material-ui/icons/CodeSharp';
import SearchIcon from '@material-ui/icons/SearchSharp';

// Logo
import PipyLogo from '../images/pipy.svg';

// CSS styles
const useStyles = makeStyles(theme => ({
  root: {
    paddingTop: '220px',
    display: 'flex',
    flexDirection: 'column',
    flexWrap: 'nowrap',
    alignItems: 'center',
  },
  logo: {
    position: 'absolute',
    width: '160px',
    left: 'calc(50% - 80px)',
    top: theme.spacing(1),
  },
  list: {
    minWidth: '350px',
  },
}));

//
// Landing page
//

function Index() {
  const classes = useStyles();

  const [openDialogNewCodebase, setOpenDialogNewCodebase] = React.useState(false);

  const queryCodebaseList = useQuery(
    'codebases',
    async () => {
      const res = await fetch('/api/v1/repo');
      if (res.status !== 200) {
        console.log('ERROR', res.status)
        const msg = await res.text();
        throw new Error(`Error: ${msg}, status = ${res.status}`);
      }
      const lines = await res.text();
      const paths = lines.split('\n').filter(l => Boolean(l)).sort();
      return paths;
    },
    {
      retry: false,
      refetchOnWindowFocus: false,
      refetchOnReconnect: false,
    }
  );

  if (queryCodebaseList.isLoading) {
    return (
      <div className={classes.root}>
        <Loading/>
      </div>
    );
  }

  if (!queryCodebaseList.isSuccess) {
    return <Codebase root="/"/>;
  }

  return (
    <div className={classes.root}>
      <img src={PipyLogo} alt='PipyLogo' className={classes.logo}/>
      <Box pb={2}>
        <Button
          variant="text"
          startIcon={<AddCircleIcon/>}
          onClick={() => setOpenDialogNewCodebase(true)}
        >
          New Codebase
        </Button>
      </Box>
      <DialogNewCodebase
        open={openDialogNewCodebase}
        onSuccess={path => navigate(`/repo${path}/`)}
        onClose={() => setOpenDialogNewCodebase(false)}
      />
      <OutlinedInput
        startAdornment={<SearchIcon/>}
        placeholder="Search..."
        margin="dense"
      />
      <Box p={2} className={classes.list}>
        <List dense>
          {queryCodebaseList.data.map(
            path => <CodebaseItem key={path} path={path}/>
          )}
        </List>
      </Box>
    </div>
  );
}

function CodebaseItem({ path }) {
  return (
    <ListItem button onClick={() => navigate(`/repo${path}/`)}>
      <ListItemIcon><CodebaseIcon/></ListItemIcon>
      <ListItemText primary={path}/>
      <ChevronRightIcon/>
    </ListItem>
  );
}

export default Index;