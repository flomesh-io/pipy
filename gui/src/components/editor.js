import React from 'react';

import { makeStyles } from '@material-ui/core/styles';
import { navigate } from 'gatsby';
import { useQueryClient, useQuery } from 'react-query';

// Material-UI components
import Button from '@material-ui/core/Button';
import Dialog from '@material-ui/core/Dialog';
import DialogActions from '@material-ui/core/DialogActions';
import DialogContent from '@material-ui/core/DialogContent';
import DialogTitle from '@material-ui/core/DialogTitle';
import Divider from '@material-ui/core/Divider';
import List from '@material-ui/core/List';
import ListItem from '@material-ui/core/ListItem';
import ListItemIcon from '@material-ui/core/ListItemIcon';
import ListItemText from '@material-ui/core/ListItemText';
import Popover from '@material-ui/core/Popover';
import TextField from '@material-ui/core/TextField';
import TreeView from '@material-ui/lab/TreeView';
import TreeItem from '@material-ui/lab/TreeItem';
import Typography from '@material-ui/core/Typography';

// Components
import DialogNewCodebase from './dialog-new-codebase';
import Pane from 'react-split-pane/lib/Pane';
import Loading from './loading';
import Nothing from './nothing';
import SplitPane from 'react-split-pane';
import Toolbar, { ToolbarButton, ToolbarTextButton, ToolbarGap, ToolbarStretch } from './toolbar';
import Working from './working';

// Icons
import AddFileIcon from '@material-ui/icons/AddSharp';
import ArrowRightIcon from '@material-ui/icons/ChevronRight';
import ArrowDownIcon from '@material-ui/icons/ExpandMore';
import BaseIcon from '@material-ui/icons/VerticalAlignTopSharp';
import CodebaseIcon from '@material-ui/icons/CodeSharp';
import CommitIcon from '@material-ui/icons/AssignmentTurnedInSharp';
import DeleteFileIcon from '@material-ui/icons/DeleteSharp';
import DerivativeIcon from '@material-ui/icons/SubdirectoryArrowRightSharp';
import FlagIcon from '@material-ui/icons/FlagSharp';
import PushIcon from '@material-ui/icons/CloudUploadSharp';
import ResetIcon from '@material-ui/icons/RotateLeft';
import SaveFileIcon from '@material-ui/icons/SaveSharp';

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
  },
  fileListPane: {
    height: '100%',
    backgroundColor: '#282828',
    padding: theme.spacing(1),
    overflow: 'auto',
  },
  codePane: {
    height: '100%',
    backgroundColor: '#202020',
  },
  folder: {
    opacity: '65%',
    fontStyle: 'italic',
  },
  inheritedFile: {
    opacity: '50%',
  },
  erasedFile: {
    color: '#f33',
    textDecorationLine: 'line-through',
  },
}));

//
// Global states
//

const splitterPos = [['360px', 1]]
const codebaseStates = {};

//
// File data
//

class File {
  constructor(name, model) {
    this.name = name;
    this.model = model;
    this.version = model.getAlternativeVersionId();

    model.onDidChangeContent(() => {
      this.onDidChangeContent?.();
    });
  }

  saved() {
    return this.model.getAlternativeVersionId() === this.version;
  }

  save() {
    this.version = this.model.getAlternativeVersionId();
  }
}

//
// File item
//

function FileItem({ path, tree, main }) {
  const classes = useStyles();
  const name = path.substring(path.lastIndexOf('/') + 1);
  if (typeof tree === 'string') {
    return (
      <TreeItem
        key={path}
        nodeId={path}
        endIcon={path === main && <FlagIcon fontSize="small" color="primary"/>}
        label={
          <Typography noWrap>
            {name}
            {tree === 'edit' && '*'}
          </Typography>
        }
        className={
          tree === "base" ? classes.inheritedFile :
          tree === "erased" ? classes.erasedFile :
          null
        }
      />
    );
  } else {
    const keys = Object.keys(tree);
    keys.sort();
    return (
      <TreeItem
        key={path}
        nodeId={path}
        label={
          <Typography noWrap className={classes.folder}>
            {name}
          </Typography>
        }
      >
        {keys.map(key => (
          <FileItem
            key={key}
            path={path + '/' + key}
            tree={tree[key]}
            main={main}
          />
        ))}
      </TreeItem>
    );
  }
}

function Editor({ root, dts }) {
  const classes = useStyles();

  const editorDiv = React.useRef();
  const editorRef = React.useRef();
  const layoutUpdaterRef = React.useRef();

  const states = React.useMemo(
    () => (
      codebaseStates[root] || (
        codebaseStates[root] = {
          tree: null,
          treeExpanded: [],
          treeSelected: '',
          files: {},
          folders: {},
          changedFiles: {},
          main: '',
          currentFile: null,
          isChanged: false,
          isConsoleOpen: true,
        }
      )
    ),
    [root]
  );

  const [isInitialized, setInitialized] = React.useState(false);
  const [isReadOnly, setReadOnly] = React.useState(true);
  const [isSelected, setSelected] = React.useState(false);
  const [isSaved, setSaved] = React.useState(states.currentFile?.saved());
  const [treeExpanded, setTreeExpanded] = React.useState(states.treeExpanded);
  const [treeSelected, setTreeSelected] = React.useState(states.treeSelected);
  const [working, setWorking] = React.useState('');
  const [openDialogNewFile, setOpenDialogNewFile] = React.useState(false);
  const [openDialogResetFile, setOpenDialogResetFile] = React.useState(false);
  const [openDialogDeleteFile, setOpenDialogDeleteFile] = React.useState(false);
  const [openDialogDeleteCodebase, setOpenDialogDeleteCodebase] = React.useState(false);

  const queryClient = useQueryClient();

  const queryCodebase = useQuery(
    `files:${root}`,
    async () => {
      const tree = {};
      const folders = {};
      const files = {};
      const changedFiles = {};
      const add = (path, type) => {
        if (type === 'edit' || type === 'erased') changedFiles[path] = true;
        const segs = path.split('/').filter(s => Boolean(s));
        const name = segs.pop();
        let k = '';
        let p = tree;
        for (const s of segs) {
          k += '/' + s;
          folders[k] = true;
          p = p[s] || (p[s] = {});
        }
        files[k + '/' + name] = null;
        p[name] = type;
      };
      const res = await fetch(isLocalHost ? '/api/v1/files' : `/api/v1/repo${root}`);
      if (res.status !== 200) {
        const msg = await res.text();
        throw new Error(`Error: ${msg}, status = ${res.status}`);
      }
      const info = await res.json();
      if (!isLocalHost) info.baseFiles?.forEach?.(path => add(path, 'base'));
      info.files?.forEach?.(path => add(path, 'file'));
      if (!isLocalHost) {
        info.editFiles?.forEach?.(path => add(path, 'edit'));
        info.erasedFiles?.forEach?.(path => add(path, 'erased'));
        states.isChanged = (info.editFiles?.length > 0 || info.erasedFiles?.length > 0);
      }
      states.tree = tree;
      states.folders = folders;
      states.files = files;
      states.changedFiles = changedFiles;
      states.main = info.main;
      if (info.readOnly) {
        editorRef.current?.updateOptions?.({ readOnly: true });
        setReadOnly(true);
      } else {
        editorRef.current?.updateOptions?.({ readOnly: false });
        setReadOnly(false);
      }
      return {
        files: Object.keys(states.files),
        base: info.base,
        derived: info.derived,
      };
    }
  );

  const isLocalHost = (root === '/');

  const updateLayout = () => {
    layoutUpdaterRef.current?.();
  }

  const selectFile = React.useCallback(
    async filename => {
      states.treeSelected = filename;
      setTreeSelected(filename);

      editorRef.current.updateOptions({ readOnly: isReadOnly });

      if (states.currentFile) {
        states.currentFile.onDidChangeContent = null;
      }

      const file = states.files[filename];
      if (file) {
        states.currentFile = file;
        file.onDidChangeContent = () => setSaved(file.saved());
        editorRef.current.setModel(file.model);
        setSelected(true);
        setSaved(file.saved());
        updateLayout();
        return;
      }

      if (filename in states.files) {
        const monaco = await import('monaco-editor');
        const res = await fetch(
          isLocalHost ? '/api/v1/files' + filename : '/api/v1/repo-files' + root + filename
        );
        if (res.status === 200) {
          const content = await res.text();
          const type = (
            filename.endsWith('.js') ? 'javascript' :
            filename.endsWith('.json') ? 'json' :
            filename.endsWith('.yaml') ? 'yaml' :
            filename.endsWith('.yml') ? 'yaml' : 'text'
          );
          const model = monaco.editor.createModel(content, type);
          const file = new File(filename, model);
          states.files[filename] = file;
          states.currentFile = file;
          file.onDidChangeContent = () => setSaved(file.saved());
          editorRef.current.setModel(model);
          setSelected(true);
          setSaved(file.saved());
          updateLayout();
          return;
        }
      }
      setSelected(false);
    },
    [root, dts, states, isReadOnly, isLocalHost]
  );

  const saveFile = React.useCallback(
    async () => {
      const file = states.currentFile;
      setWorking('Saving...');
      try {
        const res = await fetch(
          isLocalHost ? '/api/v1/files' + file.name : '/api/v1/repo-files' + root + file.name,
          {
            method: 'POST',
            headers: {
              'content-type': 'text/plain',
            },
            body: file.model.getValue(),
          }
        );
        if (res.status === 201) {
          file.save();
          if (file === states.currentFile) setSaved(file.saved());
          queryClient.invalidateQueries(`files:${root}`);
        }
      } finally {
        setWorking('');
      }
    },
    [root, states, isLocalHost, queryClient]
  );

  const changeMain = async filename => {
    setWorking('Saving...');
    try {
      const res = await fetch(
        isLocalHost ? '/api/v1/files' : '/api/v1/repo' + root,
        {
          method: 'PATCH',
          body: JSON.stringify({ main: filename }),
        }
      );
      if (res.status === 201) {
        queryClient.invalidateQueries(`files:${root}`);
      }
    } finally {
      setWorking('');
    }
  }

  const commitChanges = async (bumpVersion) => {
    setWorking('Uploading...');
    try {
      const uri = '/api/v1/repo' + root;
      const res = await fetch(uri);
      const info = await res.json();
      if (res.status === 200) {
        const ver = (parseInt(info.version)|0) + 1;
        const res = await fetch(uri, {
          method: 'PATCH',
          body: JSON.stringify(bumpVersion ? { version: ver.toString() } : {}),
        });
        if (res.status === 201) {
          queryClient.invalidateQueries(`files:${root}`);
        }
      }
    } finally {
      setWorking('');
    }
  }

  // Create Monaco editor
  React.useEffect(
    () => {
      let editor, resize;
      import('monaco-editor').then(monaco => {
        editor = monaco.editor.create(
          editorDiv.current,
          {
            theme: 'vs-dark',
            tabSize: 2,
            detectIndentation: false,
            'semanticHighlighting.enabled': true,
          }
        );
        resize = () => editor.layout();
        window.addEventListener('resize', resize);
        editorRef.current = editor;
        layoutUpdaterRef.current = resize;
        const languageDefaults = monaco.languages.typescript.javascriptDefaults;
        languageDefaults.setCompilerOptions(
          Object.assign(
            languageDefaults.getCompilerOptions(),
            {
              lib: ['lib.es5.d.ts'],
              noImplicitAny: true,
            }
          )
        );
        languageDefaults.setExtraLibs(
          Object.values(dts).map(
            content => ({ content })
          )
        );
        languageDefaults.setInlayHintsOptions({
          includeInlayParameterNameHints: 'literals',
        });
        setInitialized(true);
      });
      return () => {
        window.removeEventListener('resize', resize);
        editor?.dispose();
        editorRef.current = null;
      }
    },
    []
  );

  // Select file at initialization
  React.useEffect(
    () => {
      if (isInitialized) {
        selectFile(states.treeSelected);
      }
    },
    [isInitialized, selectFile, states.treeSelected]
  );

  // Handle Ctrl+S/Cmd+S
  React.useEffect(
    () => {
      const isMac = navigator.userAgent.indexOf('Mac OS') >= 0;
      const div = editorDiv.current;
      if (div) {
        const save = e => {
          if (e.key !== 's' && e.key !== 'S') return;
          if (isMac) {
            if (!e.metaKey) return;
          } else {
            if (!e.ctrlKey) return;
          }
          saveFile();
          e.preventDefault();
        };
        div.addEventListener('keydown', save);
        return () => div && div.removeEventListener('keydown', save);
      }
    },
    [saveFile]
  );

  const changeSplitter = (i, pos) => {
    splitterPos[i] = pos;
    updateLayout();
  }

  const handleSelectFile = (filename) => {
    selectFile(filename);
  }

  const handleExpandFolder = (nodes) => {
    states.treeExpanded = nodes;
    setTreeExpanded(nodes);
  }

  const handleClickNewFile = () => {
    setOpenDialogNewFile(true);
  }

  const handleClickSaveFile = () => {
    saveFile();
  }

  const handleClickDeleteFile = () => {
    setOpenDialogDeleteFile(true);
  }

  const handleClickSetMain = () => {
    changeMain(treeSelected);
  }

  const handleClickReset = () => {
    setOpenDialogResetFile(true);
  }

  const handleClickCommitFiles = () => {
    commitChanges(false);
  }

  const handleClickCommit = () => {
    commitChanges(true);
  }

  const handleClickDeleteCodebase = () => {
    setOpenDialogDeleteCodebase(true);
  }

  if (typeof window === 'undefined') return null;

  return (
    <div className={classes.root}>
      <Working
        open={Boolean(working)}
        text={working}
      />

      {/* Toolbar */}
      <Toolbar>
        <ToolbarButton
          disabled={isReadOnly}
          onClick={handleClickNewFile}
        >
          <AddFileIcon fontSize="small"/>
        </ToolbarButton>
        <ToolbarButton
          disabled={
            isReadOnly ||
            treeSelected === '' ||
            treeSelected === states.main ||
            treeSelected in states.folders
          }
          onClick={handleClickSetMain}
        >
          <FlagIcon fontSize="small"/>
        </ToolbarButton>
        <ToolbarButton
          disabled={
            isReadOnly || 
            treeSelected === '' ||
            treeSelected in states.folders
          }
          onClick={handleClickDeleteFile}
        >
          <DeleteFileIcon fontSize="small"/>
        </ToolbarButton>
        <ToolbarGap/>
        <ToolbarButton
          disabled={!states.currentFile || isSaved}
          onClick={handleClickSaveFile}
        >
          <SaveFileIcon fontSize="small"/>
        </ToolbarButton>
        {!isLocalHost && (
          <React.Fragment>
            <ToolbarButton
              disabled={!states.changedFiles[treeSelected]}
              onClick={handleClickReset}
            >
              <ResetIcon fontSize="small"/>
            </ToolbarButton>
            <ToolbarButton
              disabled={!states.isChanged}
              onClick={handleClickCommitFiles}
            >
              <CommitIcon fontSize="small"/>
            </ToolbarButton>
            <ToolbarButton
              disabled={!states.isChanged}
              onClick={handleClickCommit}
            >
              <PushIcon fontSize="small"/>
            </ToolbarButton>
            <ToolbarGap/>
            <ToolbarTextButton
              startIcon={<BaseIcon/>}
              disabled={queryCodebase.isRunning || !queryCodebase.data?.base}
              onClick={() => navigate(`/repo${queryCodebase.data.base}/`)}
            >
              Base
            </ToolbarTextButton>
            <ToolbarGap/>
            <DerivativesButton
              root={root}
              derived={queryCodebase.data?.derived || []}
            />
            <ToolbarGap/>
          </React.Fragment>
        )}
        <ToolbarStretch/>

        <ToolbarGap/>
        {!isLocalHost && (
          <ToolbarTextButton
            startIcon={<DeleteFileIcon/>}
            color="secondary"
            onClick={handleClickDeleteCodebase}
          >
            DELETE
          </ToolbarTextButton>
        )}
        <ToolbarGap/>
      </Toolbar>

      {/* Main View */}
      <div item className={classes.main}>
        <SplitPane split="vertical" onChange={pos => changeSplitter(0, pos)}>

          {/* File List */}
          <Pane className={classes.fileListPane} initialSize={splitterPos[0][0]}>
            {queryCodebase.isLoading ? (
              <Loading/>
            ) : (
              queryCodebase.data?.files?.length > 0 ? (
                <TreeView
                  defaultCollapseIcon={<ArrowDownIcon/>}
                  defaultExpandIcon={<ArrowRightIcon/>}
                  defaultExpanded={['/']}
                  expanded={treeExpanded}
                  selected={treeSelected}
                  onNodeToggle={(_, list) => handleExpandFolder(list)}
                  onNodeSelect={(_, node) => handleSelectFile(node)}
                >
                  {states.tree && (
                    Object.keys(states.tree).sort().map(
                      filename => (
                        <FileItem
                          key={filename}
                          path={'/' + filename}
                          tree={states.tree[filename]}
                          main={states.main}
                        />
                      )
                    )
                  )}
                </TreeView>
              ) : (
                <Nothing text="No files"/>
              )
            )}
          </Pane>

          {/* Editor */}
          <Pane className={classes.codePane} initialSize={splitterPos[0][1]}>
            <div
              ref={editorDiv}
              className={classes.codePane}
              style={{ display: isSelected ? 'block' : 'none' }}
            />
            {!isSelected && <Nothing text="No file selected"/>}
          </Pane>

        </SplitPane>
      </div>

      {/* New File Dialog */}
      <DialogNewFile
        open={openDialogNewFile}
        root={root}
        files={states.files}
        folders={states.folders}
        filename={treeSelected}
        onSuccess={filename => selectFile(filename)}
        onClose={() => setOpenDialogNewFile(false)}
      />

      {/* File Resetting Dialog*/}
      <DialogResetFile
        open={openDialogResetFile}
        root={root}
        filename={treeSelected}
        onSuccess={filename => selectFile(filename)}
        onClose={() => setOpenDialogResetFile(false)}
      />

      {/* File Deletion Dialog */}
      <DialogDeleteFile
        open={openDialogDeleteFile}
        root={root}
        filename={treeSelected}
        onSuccess={() => selectFile('')}
        onClose={() => setOpenDialogDeleteFile(false)}
      />

      {/* Codebase Deletion Dialog */}
      <DialogDeleteCodebase
        open={openDialogDeleteCodebase}
        root={root}
        onSuccess={() => navigate('/')}
        onClose={() => setOpenDialogDeleteCodebase(false)}
      />

    </div>
  );
}

function DialogNewFile({ open, root, files, folders, filename, onSuccess, onClose }) {
  const queryClient = useQueryClient();

  const [name, setName] = React.useState('');
  const [working, setWorking] = React.useState(false);
  const [error, setError] = React.useState('');

  React.useEffect(
    () => {
      if (open) {
        let basePath;
        if (filename in folders) {
          basePath = filename;
        } else {
          basePath = filename.substring(0, filename.lastIndexOf('/'));
        }
        setName(basePath + '/');
        setError('');
      }
    },
    [open, filename, folders]
  );

  const handleChangeName = evt => {
    let name = evt.target.value;
    if (!name || !name.startsWith('/')) name = '/' + name;
    setName(name);
    if (name.endsWith('/')) {
      setError('');
    } else if (name in files || name in folders) {
      setError('Filename already exists');
    } else {
      setError('');
    }
  }

  const handleClickOK = async () => {
    setWorking(true);
    try {
      const isLocalHost = (root === '/');
      const res = await fetch(
        isLocalHost ? '/api/v1/files' + name : '/api/v1/repo-files' + root + name,
        {
          method: 'POST',
          headers: {
            'content-type': 'text/plain',
          },
          body: '',
        }
      );
      if (res.status === 201) {
        queryClient.invalidateQueries(`files:${root}`);
        onSuccess(name);
        onClose();
      }
    } finally {
      setWorking(false);
    }
  }

  return (
    <Dialog open={open} fullWidth onClose={onClose}>
      <DialogTitle>Add File</DialogTitle>
      <DialogContent>
        <TextField
          label="Filename"
          value={name}
          error={Boolean(error)}
          helperText={error}
          fullWidth
          onChange={handleChangeName}
        />
      </DialogContent>
      <DialogActions>
        <Button
          disabled={!name || name.endsWith('/') || Boolean(error)}
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

function DialogResetFile({ open, root, filename, onSuccess, onClose }) {
  const queryClient = useQueryClient();

  const [working, setWorking] = React.useState(false);

  const handleClickDiscard = async () => {
    setWorking(true);
    try {
      const res = await fetch(
        '/api/v1/repo-files' + root + filename,
        {
          method: 'PATCH',
        }
      );
      if (res.status === 201) {
        queryClient.invalidateQueries(`files:${root}`);
        onSuccess();
        onClose();
      }
    } finally {
      setWorking(false);
    }
  }

  return (
    <Dialog open={open} fullWidth onClose={onClose}>
      <DialogTitle>Reset File</DialogTitle>
      <DialogContent>
        <Typography>
          Discard changes on file '{filename}'?
        </Typography>
      </DialogContent>
      <DialogActions>
        <Button color="secondary" onClick={handleClickDiscard}>
          Discard
        </Button>
        <Button onClick={onClose}>
          Cancel
        </Button>
      </DialogActions>
      <Working open={working} text="Resetting..."/>
    </Dialog>
  );
}

function DialogDeleteFile({ open, root, filename, onSuccess, onClose }) {
  const queryClient = useQueryClient();

  const [working, setWorking] = React.useState(false);

  const handleClickDelete = async () => {
    setWorking(true);
    try {
      const isLocalHost = (root === '/');
      const res = await fetch(
        isLocalHost ? '/api/v1/files' + filename : '/api/v1/repo-files' + root + filename,
        {
          method: 'DELETE',
        }
      );
      if (res.status === 204) {
        queryClient.invalidateQueries(`files:${root}`);
        onSuccess();
        onClose();
      }
    } finally {
      setWorking(false);
    }
  }

  return (
    <Dialog open={open} fullWidth onClose={onClose}>
      <DialogTitle>Delete File</DialogTitle>
      <DialogContent>
        <Typography>
          Delete the file '{filename}'?
        </Typography>
      </DialogContent>
      <DialogActions>
        <Button color="secondary" onClick={handleClickDelete}>
          Delete
        </Button>
        <Button onClick={onClose}>
          Cancel
        </Button>
      </DialogActions>
      <Working open={working} text="Deleting..."/>
    </Dialog>
  );
}

function DialogDeleteCodebase({ open, root, onSuccess, onClose }) {
  const queryClient = useQueryClient();

  const [working, setWorking] = React.useState(false);

  const handleClickDelete = async () => {
    setWorking(true);
    try {
      const res = await fetch(
        '/api/v1/repo' + root,
        {
          method: 'DELETE',
        }
      );
      if (res.status === 204) {
        queryClient.invalidateQueries(`files:${root}`);
        onSuccess();
        onClose();
      }
    } finally {
      setWorking(false);
    }
  }

  return (
    <Dialog open={open} fullWidth onClose={onClose}>
      <DialogTitle>Delete Codebase</DialogTitle>
      <DialogContent>
        <Typography>
          Delete the the entire codebase {root} and all its derived codebases?
        </Typography>
      </DialogContent>
      <DialogActions>
        <Button color="secondary" onClick={handleClickDelete}>
          Delete
        </Button>
        <Button onClick={onClose}>
          Cancel
        </Button>
      </DialogActions>
      <Working open={working} text="Deleting..."/>
    </Dialog>
  );
}

function DerivativesButton({ root, derived }) {
  const [anchorEl, setAnchorEl] = React.useState(null);
  const [openDialogNew, setOpenDialogNew] = React.useState(false);

  const handleClickDropDown = event => {
    setAnchorEl(event.currentTarget);
  }

  const handleClickNew = () => {
    setAnchorEl(null);
    setOpenDialogNew(true);
  }

  const handleSelectItem = path => {
    setAnchorEl(null);
    navigate(`/repo${path}/`);
  }

  return (
    <React.Fragment>
      <ToolbarTextButton
        startIcon={<DerivativeIcon/>}
        endIcon={<ArrowDownIcon/>}
        onClick={handleClickDropDown}
      >
        Derivatives
      </ToolbarTextButton>
      <Popover
        open={Boolean(anchorEl)}
        anchorEl={anchorEl}
        anchorOrigin={{ vertical: 'bottom', horizontal: 'left' }}
        onClose={() => setAnchorEl(null)}
      >
        <List dense>
          <ListItem button onClick={handleClickNew}>
            <ListItemIcon><AddFileIcon/></ListItemIcon>
            <ListItemText>Derive new codebase...</ListItemText>
          </ListItem>
        </List>
        {derived.length > 0 && <Divider/>}
        {derived.length > 0 && (
          <List dense>
            {derived.map(
              path => (
                <ListItem key={path} button onClick={() => handleSelectItem(path)}>
                  <ListItemIcon><CodebaseIcon/></ListItemIcon>
                  <ListItemText primary={path}/>
                </ListItem>
              )
            )}
          </List>
        )}
      </Popover>
      <DialogNewCodebase
        open={openDialogNew}
        base={root}
        onSuccess={path => navigate(`/repo${path}/`)}
        onClose={() => setOpenDialogNew(false)}
      />
    </React.Fragment>
  );
}

export default Editor;
