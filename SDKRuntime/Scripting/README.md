# X-Ray SDK Python add-ons — API 1

**Полный справочник API на русском языке:** `api/API_RU.md` рядом с этим
файлом в исходниках; `bins/scripting/api/API_RU.md` в установленном SDK.
Функции, классы, жизненный цикл, UI, сцена, Undo, C++/Lua и практические примеры.

Open **Scripts → Add-on Manager** to install, enable, disable, reload or uninstall
an add-on. New add-ons are disabled until enabled explicitly. Enable **Hello SDK**
in any editor, or **Scene Tools** in Level Editor, for working examples. Commands
appear in Scripts, panels under Scripts → Panels, and output in Python Console.
Uninstall moves an add-on to `settings/addon-trash/` so it can be recovered.

## Layout and installation

```
SDK/
  addons/                         # Your .xpy add-ons
    hello_sdk.xpy
    scene_tools/
      addon.json
      __init__.xpy
      helpers.xpy
  settings/addons.json             # Enabled add-ons, independently per editor
  settings/addons/<id>.json         # Persistent add-on preferences
  bins/python/                     # Private CPython 3.13 runtime and license
  bins/scripting/xray/__init__.py   # Host API implementation
```

The `.xpy` extension denotes ordinary UTF-8 Python source. No conversion or
encryption is involved. Multi-file add-ons support package-relative `.xpy`
imports (`from .helpers import ...`). SDK implementation/standard-library files
retain their normal Python extensions.

Install matching native binaries, then run `InstallScripting.ps1 -SdkRoot
'E:\X-Ray SDK'`. It downloads the official signed CPython embeddable distribution,
verifies a pinned archive SHA256 and DLL Authenticode signature, and installs
examples without overwriting existing examples. It does not require a system
Python installation or change PATH. The shipped package targets x64 Windows.

The manager accepts a single `.xpy` file or ZIP containing one add-on folder.
Archives are validated for escaping paths, symbolic links and size limits.
Existing add-on IDs cannot be overwritten by an installation.

## Minimal add-on

```python
addon_info = {"id": "my_tools", "name": "My Tools", "api_version": 1}
import xray

def hello(context):
    xray.log("Hello", context.editor)

def register():
    xray.operator("my_tools.hello", "Say hello", undo=False)(hello)

def unregister():
    pass
```

Save this as `addons/my_tools.xpy`. `register()` runs on enable. `unregister()`
runs on disable/reload/shutdown, with automatic removal of owned registrations,
handlers, timers, Python modules and classes even if unregister fails.

A directory add-on uses `addon.json` with `id`, `name`, `version`, `api_version: 1`,
`entry: "__init__.xpy"`, optional `editors` (level, actor, particle, shader), and
`requires` (list of other installed add-on IDs). Dependencies load first; missing
or circular dependencies fail without registering the dependent add-on. Disable
dependents before disabling/reloading a dependency. Disabled discovery reads
metadata without running the add-on's code.

## API

- `context.editor`, `context.sdk_root`, `context.selected_objects`, `context.scene`.
- `ops.list()` discovers native editor commands; `ops.execute("COMMAND_...",
  p0=0, p1=0)` accepts strings/uint32 arguments and returns the command result.
  Lifecycle commands that destroy the editor/interpreter are deliberately
  unavailable during a Python call. Commands otherwise retain editor behavior,
  including dialogs and command-specific preconditions.
- `operator(id, label, undo=True, shortcut="CTRL+SHIFT+U")(callback)` registers
  an operator taking `context`. `ops.call(id)` invokes it. Ctrl/Shift/Alt + letter
  shortcuts and Scripts menu entries invoke the same callback.
- `panel(id, label)(draw)` registers a dockable panel taking `context`.
- `Operator`/`Panel` subclasses support `bl_idname`, `bl_label`, `bl_options =
  {"UNDO"}`, `bl_shortcut`, and `execute(context)`/`draw(context)`. Register through
  `utils.register_class`, remove through `utils.unregister_class`.
- UI: `text`, `button`, `checkbox`, `input_float`, `slider_float`, `input_text`,
  `separator`, `same_line`, `collapsing_header`, `operator`. Inputs return their
  current value. Widgets are valid only in a draw callback; the host owns
  Begin/End and balances window state even when a callback raises.
- `events.subscribe(name, callback)` / `unsubscribe(token)`: startup, shutdown,
  command_executed, scene_loaded, scene_saved, scene_cleared, undo, redo,
  selection_changed. Command events carry the command name; selection_changed
  carries a list of current handles. Selection polling is throttled to 10 Hz.
- `timers.register(callback, first_interval=.1)` / `unregister(token)`. A callback
  returns the interval in seconds to run again, or None to stop. Timers run on
  the editor thread. They are automatically removed when the owner unloads.
- `preferences.load()` / `save(JSON_value)` persist the current add-on's state
  with atomic file replacement. `log(...)` and print/tracebacks reach the console.
- `app.request_exit()` queues a normal window close after the callback returns,
  preserving the editor's unsaved-scene check. Timers and command events are
  pumped on the main idle loop even when the editor window is not focused.
- `addons.refresh`, `enable(id)`, `disable(id)`, `reload(id)`, `install(path)`,
  `uninstall(id)` implement the same lifecycle as the manager.

## Level Editor scene API

`scene.objects`, `scene.selected_objects`, `scene.find(name)` return `Object`
wrappers. Object properties: name, position (XYZ), rotation (XYZ **radians**),
scale, selected, visible; class_id/reference are read-only. `obj.delete()` removes
the object. Group-owned objects that are not individually editable reject edits.
Handles are process-local and checked on access. Reacquire them after load,
clear, Undo/Redo, external rename or deletion; no raw C++ pointer is exposed.

`scene.create(kind, name="", reference="", position=(0,0,0))` supports object,
light, shape, glow and way. Object creation requires an existing `.object` library
reference. A shape starts with a unit box. Names are unique; scales must be
positive and transforms finite. Object-specific light/spawn/material/geometry
property bindings are not exposed in API 1; native commands remain discoverable.

Operators with undo enabled execute as one Level Editor transaction. Direct
edits outside a transaction create individual Undo entries. For grouped edits:

```python
with xray.scene.transaction("Lift selection"):
    for obj in xray.scene.selected_objects:
        x, y, z = obj.position
        obj.position = (x, y + 1, z)
```

An exception restores the transaction's scene snapshot. Load/clear/Undo are
rejected inside transactions; use `undo=False` for operators invoking them.
Scene object bindings currently target Level Editor; the other editors expose
their native command system and shared add-on/UI APIs.

## Execution and development

Python is embedded in the editor and add-ons have ordinary Python filesystem,
network and native-library access. This is a trusted-code extension mechanism,
not a sandbox. All editor API calls belong on the UI thread; use timers to return
work from a background task. Python bytecode callbacks have time limits; time
spent inside editor API calls is excluded from the Python budget. Native
extensions/blocking I/O cannot be preempted by those limits. Exceptions include
tracebacks, and repeated callback failures disable the offending add-on.

Reload after editing `.xpy` files. Reload cleans imported modules, registrations
and timers. The console accepts multiline Python and retains its namespace.
`Run .xpy file` executes a one-off script without installing an add-on.

The native runtime exposes one versioned bridge and an editor backend hook in
`SDKScriptHost.h`. New editor-specific operations can be added without binding
Python to internal class layouts; API consumers never receive native pointers.
