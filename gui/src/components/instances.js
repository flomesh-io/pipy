import React from 'react';

import { makeStyles } from '@material-ui/core/styles';
import { useQuery } from 'react-query';

// Material-UI components
import List from '@material-ui/core/List';
import ListItem from '@material-ui/core/ListItem';
import ListItemIcon from '@material-ui/core/ListItemIcon';
import ListItemText from '@material-ui/core/ListItemText';
import Typography from '@material-ui/core/Typography';

// Components
import TimeAgo from 'react-timeago';

// Icons
import LocalHostIcon from '@material-ui/icons/DesktopWindowsSharp';

const useStyles = makeStyles(theme => ({
  listIcon: {
    minWidth: 36,
  },
  instanceActive: {
    color: '#0c0',
  },
}));

export const InstanceContext = React.createContext();

function Instances({ root }) {
  const classes = useStyles();
  const instanceContext = React.useContext(InstanceContext);

  const queryLocalStatus = useQuery(
    'status',
    async () => {
      const res = await fetch('/api/v1/status');
      if (res.status === 200) {
        return await res.json();
      } else {
        return null;
      }
    }
  );

  const queryRemoteStatus = useQuery(
    `status:${root}`,
    async () => {
      if (root === '/') return {};
      const res = await fetch(`/api/v1/repo${root}`);
      if (res.status === 200) {
        const data = await res.json();
        return data.instances;
      } else {
        return null;
      }
    },
    {
      cacheTime: 0,
      refetchInterval: 1000,
    }
  );

  const instanceMap = { '': queryLocalStatus.data };
  if (queryRemoteStatus.data) Object.assign(instanceMap, queryRemoteStatus.data);

  const instanceList = Object.entries(instanceMap);
  instanceList.forEach(([id, inst]) => {
    if (inst) {
      inst.id = id;
    }
  });

  const now = Date.now();

  const selectInstance = (id, instance) => {
    instanceContext.setCurrentInstanceIndex(id);
    instanceContext.setCurrentInstance(instance);
  }

  return (
    <List dense disablePadding>
      {instanceList.map(
        ([id, instance]) => (
          <ListItem
            key={id}
            button
            selected={instanceContext.currentInstanceIndex === id}
            onClick={() => selectInstance(id, instance)}
          >
            <ListItemIcon className={classes.listIcon}><LocalHostIcon/></ListItemIcon>
            <ListItemText
              primary={id ? `Remote Instance #${id}` + (instance.name ? ': ' + instance.name : '') : 'Local Host'}
              secondary={
                id && (
                  <div>
                    <Typography component="p" variant="caption" noWrap>{instance.uuid}</Typography>
                    {now - instance.timestamp > 30000 ? (
                      <Typography component="p" variant="caption" noWrap>
                        {'Inactive since '}
                        <TimeAgo date={instance.timestamp}/>
                      </Typography>
                    ) : (
                      <Typography
                        component="p"
                        variant="caption"
                        noWrap
                        className={classes.instanceActive}
                      >
                        Active
                      </Typography>
                    )}
                  </div>
                )
              }
            />
          </ListItem>
        )
      )}
    </List>
  );
}

export default Instances;
