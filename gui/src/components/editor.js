import React from 'react';
import GlobalState from '../global-state';

import { makeStyles } from '@material-ui/core/styles';

// Material-UI components
import Button from '@material-ui/core/Button';
import Dialog from '@material-ui/core/Dialog';
import DialogActions from '@material-ui/core/DialogActions';
import DialogContent from '@material-ui/core/DialogContent';
import DialogTitle from '@material-ui/core/DialogTitle';
import TextField from '@material-ui/core/TextField';
import TreeView from '@material-ui/lab/TreeView';
import TreeItem from '@material-ui/lab/TreeItem';
import Typography from '@material-ui/core/Typography';

// Components
import Console from './console';
import Flowchart from './flowchart';
import Pane from 'react-split-pane/lib/Pane';
import Nothing from './nothing';
import SplitPane from 'react-split-pane';
import Toolbar, { ToolbarButton, ToolbarGap } from './toolbar';

// Icons
import ArrowRightIcon from '@material-ui/icons/ExpandMore';
import ArrowDownIcon from '@material-ui/icons/ChevronRight';
import AddFileIcon from '@material-ui/icons/AddSharp';
import ConsoleIcon from '@material-ui/icons/CallToAction';
import DeleteFileIcon from '@material-ui/icons/DeleteSharp';
import RestartIcon from '@material-ui/icons/ReplaySharp';
import SaveFileIcon from '@material-ui/icons/SaveSharp';
import StartIcon from '@material-ui/icons/PlayArrowSharp';
import StopIcon from '@material-ui/icons/StopSharp';

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
  graphPane: {
    height: '100%',
    backgroundColor: '#202020',
    padding: theme.spacing(2),
    display: 'flex',
    flexDirection: 'row',
    flexWrap: 'nowrap',
    overflow: 'auto',
  },
  graph: {
    position: 'relative',
    flexShrink: 0,
  },
  consolePane: {
    width: '100%',
    height: '100%',
    backgroundColor: '#202020',
  },
  runningProgram: {
    fontWeight: 'bold',
    color: '#0e0',
  },
}));

//
// Global states
//

const splitterPos = [
  ['260px', 1],
  [1, '200px'],
  [1, 1],
];

const store = {
  fileTree: null,
  treeExpanded: [],
  treeSelected: '',
  currentFile: null,
  isConsoleOpen: true,
};

let allFolders = null;
let allFiles = null;

function loadFileTree(tree) {
  allFolders = {};
  allFiles = {};
  const visit = (path, content) => {
    if (typeof content === 'string') {
      allFiles[path] = null;
    } else {
      allFolders[path] = true;
      for (const key in content) {
        visit(path + '/' + key, content[key]);
      }
    }
  }
  allFolders['/'] = tree;
  for (const key in tree) visit('/' + key, tree[key]);
}

function getSubTree(tree, path) {
  while (path[0] === '/') path = path.substring(1);
  const segs = path.split('/');
  while (segs.length > 1) tree = tree[segs.shift()];
  return tree;
}

//
// File data
//

class File {
  constructor(name, model) {
    this.name = name;
    this.model = model;
    this.version = model.getAlternativeVersionId();

    let graphUpdatingTimeout = null;

    const loadGraph = async () => {
      const res = await fetch('/api/graph', {
        method: 'POST',
        headers: {
          'content-type': 'text/plain',
        },
        body: this.model.getValue(),
      });
      if (res.status === 200) {
        let graph = null;
        try {
          graph = await res.json();
        } catch (err) {}
        if (graph) {
          this.graph = graph;
          this.onDidChangeGraph?.();
        }
      }
    }

    loadGraph();

    model.onDidChangeContent(() => {
      if (graphUpdatingTimeout !== null) {
        window.clearTimeout(graphUpdatingTimeout);
      }
      graphUpdatingTimeout = window.setTimeout(loadGraph, 1000);
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

function FileItem({ path, content, runningProgram }) {
  const classes = useStyles();
  const name = path.substring(path.lastIndexOf('/') + 1);
  if (typeof content === 'string') {
    return (
      <TreeItem
        key={path}
        nodeId={path}
        label={
          <Typography
            noWrap
            className={runningProgram === path ? classes.runningProgram : null}
          >
            {name}
          </Typography>
        }
      />
    );
  } else {
    const keys = Object.keys(content);
    keys.sort();
    return (
      <TreeItem
        key={path}
        nodeId={path}
        label={<Typography noWrap>{name}</Typography>}
      >
        {keys.map(key => (
          <FileItem
            key={key}
            path={path + '/' + key}
            content={content[key]}
            runningProgram={runningProgram}
          />
        ))}
      </TreeItem>
    )
  }
}

function Editor() {
  const classes = useStyles();
  const globalState = React.useContext(GlobalState);

  const editorDiv = React.useRef();
  const editorRef = React.useRef();
  const layoutUpdaterRef = React.useRef();

  const [ fileTree, setFileTree ] = React.useState(store.fileTree);
  const [ currentGraph, setCurrentGraph ] = React.useState(store.currentFile?.graph);
  const [ isSaved, setSaved ] = React.useState(store.currentFile?.saved());
  const [ treeExpanded, setTreeExpanded ] = React.useState(store.treeExpanded);
  const [ treeSelected, setTreeSelected ] = React.useState(store.treeSelected);
  const [ showConsole, setShowConsole ] = React.useState(store.isConsoleOpen);
  const [ showAddFile, setShowAddFile ] = React.useState(false);
  const [ showDeleteFile, setShowDeleteFile ] = React.useState(false);

  const saveFile = React.useCallback(
    async () => {
      const file = store.currentFile;
      globalState.showWaiting('Saving...');
      try {
        const res = await fetch('/api/files' + file.name, {
          method: 'POST',
          headers: {
            'content-type': 'text/plain',
          },
          body: file.model.getValue(),
        });
        if (res.status === 201) {
          file.save();
          if (file === store.currentFile) setSaved(file.saved());
        }
      } finally {
        globalState.showWaiting(null);
      }
    },
    [globalState]
  );

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
          }
        );
        resize = () => editor.layout();
        window.addEventListener('resize', resize);
        editorRef.current = editor;
        layoutUpdaterRef.current = resize;
        if (store.treeSelected) selectFile(store.treeSelected);
      });
      return () => {
        window.removeEventListener('resize', resize);
        editor?.dispose();
        editorRef.current = null;
      }
    },
    []
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

  // Fetch file list
  React.useEffect(
    () => {
      if (!store.fileTree) {
        (async () => {
          globalState.showWaiting('Loading working directory...');
          try {
            const res = await fetch('/api/files');
            if (res.status === 200) {
              const tree = await res.json();
              store.fileTree = tree;
              loadFileTree(tree);
              setFileTree(tree);
            }
          } finally {
            globalState.showWaiting(null);
          }
        })();
      }
    },
    [globalState]
  );

  // Update layout when open/close console
  React.useEffect(
    () => updateLayout(),
    [showConsole]
  );
  const updateLayout = () => {
    layoutUpdaterRef.current?.();
  }

  const changeSplitter = (i, pos) => {
    splitterPos[i] = pos;
    updateLayout();
  }

  const selectFile = async filename => {
    store.treeSelected = filename;
    setTreeSelected(filename);

    if (store.currentFile) {
      store.currentFile.onDidChangeContent = null;
      store.currentFile.onDidChangeGraph = null;
    }

    const file = allFiles[filename];
    if (file) {
      store.currentFile = file;
      file.onDidChangeContent = () => setSaved(file.saved());
      file.onDidChangeGraph = () => setCurrentGraph(file.graph);
      editorRef.current.setModel(file.model);
      setCurrentGraph(file.graph);
      setSaved(file.saved());
      return;
    }

    if (filename in allFiles) {
      const monaco = await import('monaco-editor');
      const res = await fetch('/api/files' + filename);
      if (res.status === 200) {
        const content = await res.text();
        const type = (
          filename.endsWith('.js') ? 'javascript' :
          filename.endsWith('.json') ? 'json' : 'text'
        );
        const model = monaco.editor.createModel(content, type);
        const file = new File(filename, model);
        allFiles[filename] = file;
        store.currentFile = file;
        file.onDidChangeContent = () => setSaved(file.saved());
        file.onDidChangeGraph = () => setCurrentGraph(file.graph);
        editorRef.current.setModel(model);
        setSaved(file.saved());
      }
    }
  }

  const startProgram = async (filename) => {
    globalState.clearLog();
    setShowConsole(true);
    globalState.showWaiting('Starting program...');
    try {
      const res = await fetch('/api/program', {
        method: 'POST',
        body: filename,
      });
      if (res.status === 201) {
        globalState.setRunningProgram(filename);
      }
    } finally {
      globalState.showWaiting(null);
    }
  }

  const handleSelectFile = (filename) => {
    selectFile(filename);
  }

  const handleExpandFolder = (nodes) => {
    store.treeExpanded = nodes;
    setTreeExpanded(nodes);
  }

  const handleClickAddFile = () => {
    setShowAddFile(true);
  }

  const handleClickSaveFile = () => {
    saveFile();
  }

  const handleClickDeleteFile = () => {
    setShowDeleteFile(true);
  }

  const handleAddFile = async (filename) => {
    setShowAddFile(false);
    globalState.showWaiting('Saving...');
    try {
      const res = await fetch('/api/files' + filename, {
        method: 'POST',
        headers: {
          'content-type': 'text/plain',
        },
        body: '',
      });
      if (res.status === 201) {
        const p = filename.lastIndexOf('/');
        const base = filename.substring(0,p);
        const name = filename.substring(p+1);
        const newTree = { ...fileTree };
        const subTree = getSubTree(newTree, base);
        subTree[name] = '';
        allFiles[filename] = null;
        store.fileTree = newTree;
        setFileTree(newTree);
        await selectFile(filename);
      }
    } finally {
      globalState.showWaiting(null);
    }
  }

  const handleDeleteFile = async filename => {
    setShowDeleteFile(false);
    globalState.showWaiting('Deleting...');
    try {
      const res = await fetch('/api/files' + filename, {
        method: 'DELETE',
      });
      if (res.status === 204) {
        const p = filename.lastIndexOf('/');
        const base = filename.substring(0,p);
        const name = filename.substring(p+1);
        const newTree = { ...fileTree };
        const subTree = getSubTree(newTree, base);
        delete subTree[name];
        delete allFiles[filename];
        store.currentFile.model.dispose();
        store.currentFile = null;
        store.fileTree = newTree;
        setFileTree(newTree);
        selectFile('');
      }
    } finally {
      globalState.showWaiting(null);
    }
  }

  const handleStart = async () => {
    startProgram(store.currentFile.name);
  }

  const handleRestart = async () => {
    startProgram(globalState.runningProgram);
  }

  const handleStop = async () => {
    globalState.showWaiting('Stopping program...');
    try {
      const res = await fetch('/api/program', {
        method: 'DELETE',
      });
      if (res.status === 204) {
        globalState.setRunningProgram('');
      }
    } finally {
      globalState.showWaiting(null);
    }
  }

  const handleShowConsole = show => {
    store.isConsoleOpen = show;
    setShowConsole(show);
  }

  if (typeof window === 'undefined') return null;

  return (
    <div className={classes.root}>

      {/* Toolbar */}
      <Toolbar>
        <ToolbarButton
          onClick={handleClickAddFile}
        >
          <AddFileIcon fontSize="small"/>
        </ToolbarButton>
        <ToolbarButton
          disabled={!treeSelected || treeSelected in allFolders}
          onClick={handleClickDeleteFile}
        >
          <DeleteFileIcon fontSize="small"/>
        </ToolbarButton>
        <ToolbarGap/>
        <ToolbarButton
          disabled={!store.currentFile || isSaved}
          onClick={handleClickSaveFile}
        >
          <SaveFileIcon fontSize="small"/>
        </ToolbarButton>
        <ToolbarButton
          disabled={
            !store.currentFile ||
             store.currentFile.name === globalState.runningProgram ||
            !store.currentFile.name.endsWith('.js')}
          onClick={handleStart}
        >
          <StartIcon fontSize="small"/>
        </ToolbarButton>
        <ToolbarButton
          disabled={!globalState.runningProgram}
          onClick={handleStop}
        >
          <StopIcon fontSize="small"/>
        </ToolbarButton>
        <ToolbarButton
          disabled={!globalState.runningProgram}
          onClick={handleRestart}
        >
          <RestartIcon fontSize="small"/>
        </ToolbarButton>
        <ToolbarGap/>
        <ToolbarButton
          onClick={() => handleShowConsole(!showConsole)}
        >
          <ConsoleIcon fontSize="small"/>
        </ToolbarButton>
      </Toolbar>

      {/* Main View */}
      <div item className={classes.main}>
        <SplitPane split="vertical" onChange={pos => changeSplitter(0, pos)}>

          {/* File List */}
          <Pane className={classes.fileListPane} initialSize={splitterPos[0][0]}>
            <TreeView
              defaultCollapseIcon={<ArrowRightIcon/>}
              defaultExpandIcon={<ArrowDownIcon/>}
              defaultExpanded={[ '/' ]}
              expanded={treeExpanded}
              selected={treeSelected}
              onNodeToggle={(_, list) => handleExpandFolder(list)}
              onNodeSelect={(_, node) => handleSelectFile(node)}
            >
              {fileTree && (
                Object.keys(fileTree).sort().map(
                  filename => (
                    <FileItem
                      key={filename}
                      path={'/' + filename}
                      content={fileTree[filename]}
                      runningProgram={globalState.runningProgram}
                    />
                  )
                )
              )}
            </TreeView>
          </Pane>

          <SplitPane
            split="horizontal"
            initialSize={splitterPos[0][1]}
            onChange={pos => changeSplitter(1, pos)}
          >
            <SplitPane split="vertical" initialSize={splitterPos[1][0]} onChange={pos => changeSplitter(2, pos)}>

              {/* Editor */}
              <Pane initialSize={splitterPos[2][0]}>
                <div ref={editorDiv} className={classes.codePane}/>
              </Pane>

              {/* Flowchart */}
              <Pane className={classes.graphPane} initialSize={splitterPos[2][1]}>
                {currentGraph?.roots?.length ? (
                  currentGraph.roots.map(
                    root => (
                      <div key={root} className={classes.graph}>
                        <Flowchart nodes={currentGraph.nodes} root={root}/>
                      </div>
                    )
                  )
                ) : (
                  <Nothing text="No pipelines"/>
                )}
              </Pane>
            </SplitPane>

            {/* Console */}
            {showConsole && (
              <Pane className={classes.consolePane} initialSize={splitterPos[1][1]}>
                <Console onClose={() => handleShowConsole(false)}/>
              </Pane>
            )}

          </SplitPane>
        </SplitPane>
      </div>

      {/* New File Dialog */}
      <DialogAddFile
        show={showAddFile}
        currentFilename={treeSelected}
        onClose={() => setShowAddFile(false)}
        onAddFile={handleAddFile}
      />

      {/* File Deletion Dialog */}
      <DialogDeleteFile
        show={showDeleteFile}
        filename={treeSelected}
        onClose={() => setShowDeleteFile(false)}
        onDeleteFile={handleDeleteFile}
      />

    </div>
  );
}

function DialogAddFile({ show, currentFilename, onClose, onAddFile }) {
  const [ inputFilename, setInputFilename ] = React.useState('');
  const [ showError, setShowError ] = React.useState(false);
  const [ errorInfo, setErrorInfo ] = React.useState('');

  React.useEffect(() => {
    if (show) {
      setInputFilename('');
      setShowError(false);
      setErrorInfo('');
    }
  }, [show]);

  const getFullInputFilename = filename => {
    let base = (
      currentFilename in allFolders ? (
        currentFilename
      ) : (
        currentFilename.substring(0, currentFilename.lastIndexOf('/'))
      )
    );
    if (!base.endsWith('/')) base += '/';
    return base + filename;
  };

  const handleChangeFilename = evt => {
    const value = evt.target.value;
    setInputFilename(value);
    if (!value) {
      setShowError(false);
      setErrorInfo('');
    } else {
      const filename = getFullInputFilename(value);
      if (filename in allFiles || filename in allFolders) {
        setShowError(true);
        setErrorInfo('Filename already exists');
      } else {
        setShowError(false);
        setErrorInfo('');
      }  
    }
  }

  const handleClickOK = () => {
    onAddFile(getFullInputFilename(inputFilename));
  }

  return (
    <Dialog open={show} fullWidth onClose={onClose}>
      <DialogTitle>Add File</DialogTitle>
      <DialogContent>
        <TextField
          label="Filename"
          value={inputFilename}
          error={showError}
          helperText={errorInfo}
          fullWidth
          onChange={handleChangeFilename}
        />
      </DialogContent>
      <DialogActions>
        <Button disabled={!inputFilename || showError} onClick={handleClickOK}>
          Create
        </Button>
        <Button onClick={() => onClose()}>
          Cancel
        </Button>
      </DialogActions>
    </Dialog>
  );
}

function DialogDeleteFile({ show, filename, onClose, onDeleteFile }) {
  const handleClickDelete = () => {
    onDeleteFile(filename);
  }

  return (
    <Dialog open={show} fullWidth onClose={onClose}>
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
    </Dialog>
  );
}

export default Editor;
