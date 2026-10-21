# Python → C++ bridge (ABI 1)

`SDKScriptCall(operation, text, x, y, z)` is exported by `SDKRuntime.dll`.
Python's `ctypes.PyDLL` preserves the GIL while the main-thread editor call runs.
No pointers to editor objects cross the boundary. Object handles are numeric,
session-local identities validated by the Level Editor backend on every access.

The result is UTF-8 JSON: `{"ok": value}` or `{"error": message}`. C++ exceptions
and recoverable editor assertion failures become Python RuntimeError. Never hold
the returned pointer past the next native call; `ctypes.c_char_p` copies it.

The text field encodes strings; operations needing several strings separate them
with ASCII unit separator (`\x1f`). NUL/unit-separator characters are rejected by
the Python wrapper. Numbers use x/y/z and must be finite. Native command integer
arguments are uint32. SDK string/code-page conversion is performed at the native
boundary; JSON, module paths, Python source and UI labels are UTF-8.

| Operation | text | x/y/z | Result |
|---|---|---|---|
| context | empty | unused | editor, sdk_root, api_version |
| commands | empty | unused | native command names |
| execute | command / typed arg / typed arg | unused | native command result |
| register_operator | id / label / owner / shortcut | unused | null |
| register_panel | id / label / owner / empty | unused | null |
| owner_remove | owner | unused | null; cleanup registrations and Lua state |
| log | message | unused | null |
| ui.* | widget label/text | values | widget value |
| scene.objects / scene.selected | empty | unused | array of handles |
| scene.find | object name | unused | handle or null |
| scene.create | kind / name / reference | XYZ position | handle |
| scene.begin / commit / rollback | label for begin | unused | null |
| object.get | handle | unused | object properties |
| object.position / rotation / scale | handle | XYZ | null |
| object.rename | handle / name | unused | null |
| object.select / visible | handle | boolean x | null |
| object.delete | handle | unused | null |
| lua.eval / lua.execute | owner / Lua source | unused | converted return value |
| lua.reset | owner | unused | null |

Native command arguments use `s` followed by a string, or `i` followed by a uint32.
Example: execute `COMMAND_LOAD\x1fsE:\maps\example.level\x1fi0`.

Use the public Python wrappers instead of manually encoding bridge operations.
Registration/owner_remove and scene.invalidate are host internals, not extension
points for scripts to bypass another owner's lifecycle.

## Extending C++ bindings

`Editors/XrECore/Editor/SDKScriptHost.h` declares the native backend function
type. A backend receives operation/text/numbers and returns an encoded JSON value
or throws a C++ exception. Register it with `SDKScript::SetBackend` before the
first UI frame. Keep editor-specific code in its editor module; the shared host
owns interpreter, UI widgets, console, add-on registrations and command events.
`Editors/LevelEditor/Kernel/SDKScriptScene.cpp` is the reference implementation.

Add a native operation, a corresponding wrapper in `python/xray/__init__.py`, a
typed declaration here, and behavioral tests. Declare pointer lifetime, undo,
locking and event behavior for each new operation. Internal C++ class instances
are never exposed as `ctypes` pointers. Mutations must use editor notifications
and Undo rather than only writing object fields.
