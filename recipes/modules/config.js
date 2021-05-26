pipy({
  _g: {
    configVersion: 0,
  },

  _configVersion: undefined,
  _configUpdated: undefined,
})

.pipeline('check')
  .onMessageStart(
    () => (
      _configVersion = os.stat(__argv[0])?.mtime | 0,
      _configVersion !== _g.configVersion && (
        _g.configVersion = _configVersion,
        _configUpdated = true
      )
    )
  )
  .link(
    'update-config', () => _configUpdated,
    'end-of-task'
  )

// Update configuration
.pipeline('update-config')
  .onSessionStart(
    () => (
      console.log('Updating configuration...'),
      __argv[1](JSON.decode(os.readFile(__argv[0]))),
      console.log('Configuration updated.')
    )
  )
  .link('end-of-task')

// End of task
.pipeline('end-of-task')
  .replaceMessage(new SessionEnd)