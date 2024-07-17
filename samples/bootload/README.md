## Bootloader and Auto-updater

This sample demonstrates a simple script bootloader that is able to check remote codebase updates, download updates and restart script automatically. Downloaded script is kept locally so that the program can still start up successfully after a download failure.

## Usage

To start the bootloader, type:

```sh
pipy bootload/main.js --args http://pipy-repo-address:6060/repo/my-codebase/
```

Or, if `pipy` can be found in the current `$PATH`, you can execute it just by:

```sh
bootload/main.js http://pipy-repo-address:6060/repo/my-codebase/
```

Downloaded script will be saved in the current directory. If a different directory is preferred, add a second argument to the command:

```sh
bootload/main.js http://pipy-repo-address:6060/repo/my-codebase/ /path/to/local/backup/folder
```
