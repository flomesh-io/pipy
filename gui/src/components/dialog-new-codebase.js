import React from 'react';

import { useQueryClient, useQuery } from 'react-query';

// Material-UI components
import Button from '@material-ui/core/Button';
import Dialog from '@material-ui/core/Dialog';
import DialogActions from '@material-ui/core/DialogActions';
import DialogContent from '@material-ui/core/DialogContent';
import DialogTitle from '@material-ui/core/DialogTitle';
import TextField from '@material-ui/core/TextField';

// Components
import Working from './working';

function DialogNewCodebase({ open, base, onSuccess, onClose }) {
  const [name, setName] = React.useState('/');
  const [working, setWorking] = React.useState(false);
  const [error, setError] = React.useState('');

  React.useEffect(
    () => open && setName('/'),
    [open]
  );

  const queryCodebases = useQuery('codebaseNames', async () => {
    const res = await fetch('/api/v1/repo');
    if (res.status !== 200) {
      const msg = await res.text();
      throw new Error(`Error: ${msg}, status = ${res.status}`);
    }
    const lines = await res.text();
    const all = {};
    lines.split('\n').forEach(l => {
      if (l) all[l] = true;
    });
    return all;
  });

  const queryClient = useQueryClient();

  const handleChangeName = evt => {
    let value = evt.target.value;
    if (!value) {
      setName('/');
      setError('');
    } else {
      if (!value.startsWith('/')) value = '/' + value;
      setName(value);
      const allNames = queryCodebases.data;
      if (allNames) {
        if (allNames[value]) {
          setError('Codebase name is in use');
        } else {
          const segs = value.split('/').filter(s => Boolean(s));
          let prefix = '';
          if (segs.some(s => allNames[prefix += '/' + s])) {
            setError('Codebase name is under an existing name');
          } else {
            setError('');
          }
        }
      } else {
        setError('');
      }
    }
  }

  const handleClickOK = async () => {
    setWorking(true);
    try {
      const segs = name.split('/').filter(s => Boolean(s));
      const path = segs.join('/');
      const res = await fetch(
        `/api/v1/repo/${path}`,
        {
          method: 'POST',
          body: JSON.stringify({
            version: 1,
            base,
          }),
        }
      );
      if (res.status !== 201) {
        const msg = await res.text();
        throw new Error(`Error: ${msg}, status = ${res.status}`);
      }
      queryClient.invalidateQueries('codebases');
      onSuccess('/' + path);
      onClose();
    } catch (err) {
      setError(err.message);
    } finally {
      setWorking(false);
    }
  }

  return (
    <Dialog open={open} fullWidth onClose={onClose}>
      <DialogTitle>{base ? 'Derive Codebase' : 'New Codebase'}</DialogTitle>
      <DialogContent>
        <TextField
          label="Codebase name"
          value={name}
          error={Boolean(error)}
          helperText={error || 'Tip: Use a path-like prefix to organize your codebases'}
          fullWidth
          onChange={handleChangeName}
        />
      </DialogContent>
      <DialogActions>
        <Button
          disabled={working || name === '/' || Boolean(error)}
          onClick={handleClickOK}
        >
          Create
        </Button>
        <Button onClick={() => onClose()}>
          Cancel
        </Button>
      </DialogActions>
      <Working open={working} text="Saving..."/>
    </Dialog>
  );
}

export default DialogNewCodebase;