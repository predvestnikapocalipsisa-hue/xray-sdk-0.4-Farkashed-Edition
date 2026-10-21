# Python → LuaJIT

```python
import xray

assert xray.lua.eval("2 + 3") == 5
xray.lua.execute("counter = (counter or 0) + 1")
assert xray.lua.eval("counter") == 1
value = xray.lua.execute('return {name="example", values={1, 2, 3}}')
assert value == {"name": "example", "values": [1, 2, 3]}
xray.lua.execute("function echo(value) return value end")
assert xray.lua.call("echo", {"text": "Привет", "numbers": [1, 2]}) == {"text": "Привет", "numbers": [1, 2]}
xray.lua.reset()
```

Each enabled add-on owns its own Lua state; interactive scripts share the console
state. Variables persist between calls by that owner and are removed on disable,
reload, reset or editor shutdown. States do not share the engine's Lua globals
and do not modify the game/server Lua VM.

`lua.call(name, *arguments)` passes JSON-compatible Python values to a named Lua
function, including nested arrays/dictionaries and UTF-8 strings.
Lua nil/boolean/finite number/UTF-8 string become Python None/bool/int-or-float/str.
Dense one-based arrays become lists; string-keyed tables become dictionaries.
Empty tables become dictionaries. Cycles, deeply nested tables, mixed key tables,
functions and native userdata raise RuntimeError. The first returned Lua value
is used; no value means None. Syntax/runtime errors propagate as RuntimeError.

Lua has `sdk.log("text")` and `sdk.command("COMMAND_NAME", p0, p1)`. The latter
returns the bridge's JSON response as a string; Python can decode it if needed.
Lua may also use ordinary LuaJIT libraries. JIT compilation is disabled for these
states so instruction hooks can impose a two-second execution budget. Blocking
native library calls are not preemptible. This is trusted add-on execution, not
a sandbox or permission boundary.
