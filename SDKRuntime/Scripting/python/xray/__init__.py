"""X-Ray SDK scripting API v1. Add-on source files use the .xpy extension."""
from __future__ import annotations
import ast
import contextlib
import ctypes
import importlib.abc
import importlib.machinery
import importlib.util
import json
import hashlib
import math
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import threading
import time
import traceback
import types
import zipfile

API_VERSION = 1
_native = ctypes.PyDLL("SDKRuntime.dll").SDKScriptCall
_native.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_double, ctypes.c_double, ctypes.c_double]
_native.restype = ctypes.c_char_p
_owner = "console"
_operators = {}
_panels = {}
_handlers = []
_timers = []
_classes = {}
_context = {}
_selection = None
_selection_check = 0.0
_console_globals = {}
_budget_state = threading.local()


def _call(operation, text="", x=0, y=0, z=0):
    if not all(math.isfinite(float(v)) for v in (x, y, z)):
        raise ValueError("Non-finite numbers are not supported")
    previous_trace = sys.gettrace()
    sys.settrace(None)
    started = time.monotonic()
    try:
        raw = _native(operation.encode(), str(text).encode("utf-8"), x, y, z)
    finally:
        elapsed = time.monotonic() - started
        for budget in getattr(_budget_state, "stack", []):
            if not budget["suspended"]: budget["deadline"] += elapsed
        sys.settrace(previous_trace)
    result = json.loads(raw)
    if "error" in result:
        raise RuntimeError(result["error"])
    return result["ok"]


def _join(*values):
    values = [str(v) for v in values]
    if any("\x1f" in v or "\0" in v for v in values):
        raise ValueError("Strings cannot contain NUL or unit separator")
    return "\x1f".join(values)


def log(*values):
    _call("log", " ".join(str(v) for v in values))


class _Stream:
    encoding = "utf-8"
    def __init__(self): self.pending = ""
    def write(self, value):
        self.pending += str(value)
        while "\n" in self.pending:
            line, self.pending = self.pending.split("\n", 1)
            log(line)
        if len(self.pending) > 8192:
            log(self.pending[:8192]); self.pending = self.pending[8192:]
        return len(value)
    def flush(self):
        if self.pending: log(self.pending); self.pending = ""
    def isatty(self): return False


@contextlib.contextmanager
def _owned(owner):
    global _owner
    previous, _owner = _owner, owner
    try: yield
    finally: _owner = previous


def _guard(callback, owner, *args, budget=2.0):
    """Bound Python bytecode execution; this is not a security sandbox."""
    previous = sys.gettrace()
    started = time.monotonic()
    stack = getattr(_budget_state, "stack", None)
    if stack is None: stack = _budget_state.stack = []
    ancestors = [(item, item["deadline"]) for item in stack]
    for item, _ in ancestors: item["suspended"] += 1
    current = {"deadline": started + budget, "suspended": 0}
    stack.append(current)
    def trace(frame, event, argument):
        if not current["suspended"] and time.monotonic() > current["deadline"]:
            raise TimeoutError(f"Python callback exceeded {budget:g} seconds")
        return trace
    try:
        with _owned(owner):
            sys.settrace(trace)
            value = callback(*args)
            sys.settrace(None)
            return True, value
    except BaseException:
        sys.settrace(None)
        details = traceback.format_exc()
        log(details)
        if owner in addons.records:
            record = addons.records[owner]
            record["error"] = details
            record["failures"] = record.get("failures", 0) + 1
            if record["failures"] >= 3:
                addons._disable(owner, call_unregister=False)
                log(f"Disabled {owner} after repeated callback errors")
        return False, None
    finally:
        sys.settrace(None)
        stack.pop()
        elapsed = time.monotonic() - started
        for item, deadline in ancestors:
            item["deadline"] = deadline + elapsed
            item["suspended"] -= 1
        sys.settrace(previous)


class _UI:
    def text(self, value): _call("ui.text", value)
    label = text
    def button(self, label): return _call("ui.button", label)
    def checkbox(self, label, value): return _call("ui.checkbox", label, bool(value))
    def input_float(self, label, value, step=0.1, fast_step=1.0): return _call("ui.float", label, value, step, fast_step)
    def slider_float(self, label, value, minimum, maximum):
        if minimum > maximum: raise ValueError("Invalid slider bounds")
        return _call("ui.slider", label, value, minimum, maximum)
    def input_text(self, label, value): return _call("ui.input", _join(label, value))
    def separator(self): _call("ui.separator")
    def same_line(self): _call("ui.same_line")
    def collapsing_header(self, label): return _call("ui.header", label)
    def operator(self, idname, label=None):
        if self.button(label or _operators[idname]["label"]): return _invoke(idname)


ui = _UI()


class Object:
    """Opaque scene handle. Becomes invalid after deletion, load, clear or Undo."""
    def __init__(self, handle): self.handle = int(handle)
    def _get(self): return _call("object.get", str(self.handle))
    def _set_vector(self, kind, value):
        if len(value) != 3: raise ValueError("Expected three coordinates")
        _call("object." + kind, str(self.handle), *value)
    @property
    def name(self): return self._get()["name"]
    @name.setter
    def name(self, value): _call("object.rename", _join(self.handle, value))
    @property
    def position(self): return tuple(self._get()["position"])
    @position.setter
    def position(self, value): self._set_vector("position", value)
    @property
    def rotation(self): return tuple(self._get()["rotation"])
    @rotation.setter
    def rotation(self, value): self._set_vector("rotation", value)
    @property
    def scale(self): return tuple(self._get()["scale"])
    @scale.setter
    def scale(self, value): self._set_vector("scale", value)
    @property
    def selected(self): return self._get()["selected"]
    @selected.setter
    def selected(self, value): _call("object.select", str(self.handle), bool(value))
    @property
    def visible(self): return self._get()["visible"]
    @visible.setter
    def visible(self, value): _call("object.visible", str(self.handle), bool(value))
    @property
    def class_id(self): return self._get()["class_id"]
    @property
    def reference(self): return self._get().get("reference")
    def delete(self): _call("object.delete", str(self.handle))
    def __repr__(self):
        try: return f"<xray.Object {self.name!r} handle={self.handle}>"
        except RuntimeError: return f"<xray.Object invalid handle={self.handle}>"


class _Scene:
    def __init__(self): self._depth = 0; self._failed = False
    @property
    def objects(self): return [Object(handle) for handle in _call("scene.objects")]
    @property
    def selected_objects(self): return [Object(handle) for handle in _call("scene.selected")]
    def find(self, name):
        handle = _call("scene.find", name)
        return Object(handle) if handle else None
    def create(self, kind, name="", reference="", position=(0, 0, 0)):
        return Object(_call("scene.create", _join(kind, name, reference), *position))
    @contextlib.contextmanager
    def transaction(self, label="Python operation"):
        outer = self._depth == 0
        if outer:
            _call("scene.begin", label)
            self._failed = False
        self._depth += 1
        try:
            yield
        except BaseException:
            self._failed = True
            raise
        finally:
            self._depth -= 1
            if outer:
                _call("scene.rollback" if self._failed else "scene.commit")


scene = _Scene()


class _Context:
    @property
    def editor(self): return _context["editor"]
    @property
    def sdk_root(self): return Path(_context["sdk_root"])
    @property
    def scene(self): return scene
    @property
    def selected_objects(self): return scene.selected_objects


context = _Context()
Context = _Context


class _Ops:
    def list(self): return _call("commands")
    def execute(self, command, p0=0, p1=0):
        def encode(value):
            if isinstance(value, str): return "s" + value
            if not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF: raise ValueError("Command argument must be a string or uint32")
            return "i" + str(int(value))
        return _call("execute", _join(command, encode(p0), encode(p1)))
    def call(self, idname): return _invoke(idname)


ops = _Ops()


def operator(idname, label=None, *, undo=True, shortcut=""):
    def register(callback):
        if idname in _operators: raise ValueError(f"Operator already registered: {idname}")
        shortcut_normalized = shortcut.upper().replace(" ", "")
        if shortcut_normalized and not re.fullmatch(r"(?:(?:CTRL|SHIFT|ALT)\+)*[A-Z]", shortcut_normalized):
            raise ValueError("Shortcuts support CTRL/SHIFT/ALT and a letter")
        title = label or idname
        _call("register_operator", _join(idname, title, _owner, shortcut_normalized))
        _operators[idname] = {"callback": callback, "label": title, "owner": _owner, "undo": undo, "shortcut": shortcut_normalized}
        return callback
    return register


def panel(idname, label=None):
    def register(callback):
        if idname in _panels: raise ValueError(f"Panel already registered: {idname}")
        _call("register_panel", _join(idname, label or idname, _owner, ""))
        _panels[idname] = {"callback": callback, "owner": _owner, "label": label or idname}
        return callback
    return register


class Operator:
    bl_idname = ""
    bl_label = ""
    bl_options = {"UNDO"}
    bl_shortcut = ""
    def execute(self, context): raise NotImplementedError


class Panel:
    bl_idname = ""
    bl_label = ""
    def draw(self, context): raise NotImplementedError


class _Utils:
    def register_class(self, cls):
        if cls in _classes: raise ValueError("Class already registered")
        instance = cls()
        if isinstance(instance, Operator):
            operator(cls.bl_idname, cls.bl_label, undo="UNDO" in cls.bl_options, shortcut=cls.bl_shortcut)(instance.execute)
        elif isinstance(instance, Panel): panel(cls.bl_idname, cls.bl_label)(instance.draw)
        else: raise TypeError("Expected an Operator or Panel subclass")
        _classes[cls] = (instance, _owner)
    def unregister_class(self, cls):
        # Native registrations are rebuilt for this owner so no stale menu entries remain.
        instance, owner = _classes.pop(cls)
        _operators.pop(cls.bl_idname, None); _panels.pop(cls.bl_idname, None)
        _call("owner_remove", owner)
        for key, value in list(_operators.items()):
            if value["owner"] == owner: _call("register_operator", _join(key, value["label"], owner, value["shortcut"]))
        for key, value in list(_panels.items()):
            if value["owner"] == owner: _call("register_panel", _join(key, value["label"], owner, ""))


utils = _Utils()


class _Events:
    def subscribe(self, event, callback):
        token = object(); _handlers.append((token, event, callback, _owner)); return token
    def unsubscribe(self, token): _handlers[:] = [item for item in _handlers if item[0] is not token]


events = _Events()


class _Timers:
    def register(self, callback, first_interval=0.1):
        if not math.isfinite(first_interval) or first_interval < 0: raise ValueError("Invalid interval")
        token = object(); _timers.append([token, callback, _owner, time.monotonic() + first_interval]); return token
    def unregister(self, token): _timers[:] = [item for item in _timers if item[0] is not token]


timers = _Timers()
app = types.SimpleNamespace(api_version=API_VERSION, timers=timers, events=events)
app.request_exit = lambda: _call("request_exit")


def _atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".write-", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2); stream.flush(); os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary): os.unlink(temporary)


@contextlib.contextmanager
def _state_lock():
    if os.name != "nt":
        yield
        return
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_wchar_p]
    kernel.CreateMutexW.restype = ctypes.c_void_p
    kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    kernel.WaitForSingleObject.restype = ctypes.c_ulong
    kernel.ReleaseMutex.argtypes = [ctypes.c_void_p]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    suffix = hashlib.sha256(str(context.sdk_root.resolve()).lower().encode()).hexdigest()[:32]
    handle = kernel.CreateMutexW(None, False, "Local\\XRaySDK.AddonState." + suffix)
    if not handle: raise OSError(ctypes.get_last_error(), "Cannot lock add-on state")
    owned = False
    try:
        owned = kernel.WaitForSingleObject(handle, 1000) in (0, 128)
        if not owned: raise RuntimeError("Another editor is saving add-on settings; retry")
        yield
    finally:
        if owned: kernel.ReleaseMutex(handle)
        kernel.CloseHandle(handle)


class _Preferences:
    def _path(self):
        if not re.fullmatch(r"[a-zA-Z0-9_.-]+", _owner): raise ValueError("Invalid preference owner")
        return context.sdk_root / "settings" / "addons" / (_owner + ".json")
    def load(self):
        path = self._path()
        return json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
    def save(self, value): _atomic_json(self._path(), value)


preferences = _Preferences()


class _Lua:
    def eval(self, expression): return _call("lua.eval", _join(_owner, expression))
    def execute(self, source): return _call("lua.execute", _join(_owner, source))
    def reset(self): _call("lua.reset", _owner)
    def call(self, name, *arguments):
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*", name):
            raise ValueError("Expected a Lua function name")
        def literal(value, depth=0):
            if depth > 16: raise ValueError("Lua argument is cyclic or too deep")
            if value is None: return "nil"
            if isinstance(value, bool): return "true" if value else "false"
            if isinstance(value, (float, int)):
                if not math.isfinite(value): raise ValueError("Non-finite Lua number")
                return repr(value)
            if isinstance(value, str):
                return '"' + ''.join(chr(b) if 32 <= b < 127 and b not in (34, 92) else '\\%03d' % b for b in value.encode("utf-8")) + '"'
            if isinstance(value, (list, tuple)): return "{" + ",".join(literal(v, depth+1) for v in value) + "}"
            if isinstance(value, dict) and all(isinstance(k, str) for k in value):
                return "{" + ",".join("[" + literal(k, depth+1) + "]=" + literal(v, depth+1) for k, v in value.items()) + "}"
            raise TypeError("Lua arguments must be JSON-compatible values")
        return self.eval(name + "(" + ",".join(literal(v) for v in arguments) + ")")


lua = _Lua()


class _XpyFinder(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if not path: return None
        leaf = fullname.rsplit(".", 1)[-1]
        for directory in path:
            base = Path(directory)
            if not base.is_relative_to(addons.directory): continue
            package, module = base / leaf / "__init__.xpy", base / (leaf + ".xpy")
            selected = package if package.is_file() else module
            if selected.is_file():
                return importlib.util.spec_from_file_location(fullname, selected,
                    loader=importlib.machinery.SourceFileLoader(fullname, str(selected)),
                    submodule_search_locations=[str(selected.parent)] if selected == package else None)


class _Addons:
    def __init__(self): self.records = {}; self.directory = Path(); self._loading = set()
    def _state_path(self): return context.sdk_root / "settings" / "addons.json"
    def _state(self):
        path = self._state_path()
        try:
            value = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
            if not isinstance(value, dict): raise ValueError("Expected a JSON object")
            return value
        except (ValueError, OSError) as error:
            log(f"Cannot read add-on preferences: {error}")
            return {}
    def _save(self):
        with _state_lock():
            state = self._state()
            state[context.editor] = [key for key, value in self.records.items() if value.get("enabled")]
            _atomic_json(self._state_path(), state)
    def _describe(self, path):
        if path.is_symlink(): raise ValueError("Symbolic link add-ons are not supported")
        if path.is_dir():
            manifest = path / "addon.json"
            if not manifest.exists(): return None
            info = json.loads(manifest.read_text(encoding="utf-8-sig"))
            entry = (path / info.get("entry", "__init__.xpy")).resolve()
            if not entry.is_relative_to(path.resolve()): raise ValueError("Entry point escapes add-on directory")
        else:
            if path.suffix.lower() != ".xpy": return None
            info = {}
            tree = ast.parse(path.read_text(encoding="utf-8-sig"), filename=str(path))
            for node in tree.body:
                if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "addon_info" for t in node.targets):
                    info = ast.literal_eval(node.value); break
            entry = path.resolve()
        identifier = info.get("id", path.stem)
        if not re.fullmatch(r"[a-z][a-z0-9_]{0,63}", identifier): raise ValueError("Invalid add-on id")
        if entry.suffix.lower() != ".xpy" or not entry.is_file(): raise ValueError("Entry point must be an existing .xpy file")
        if info.get("api_version", 1) != API_VERSION: raise ValueError("Unsupported add-on API version")
        requires = info.get("requires", [])
        editors = info.get("editors", ["level", "actor", "particle", "shader"])
        if not isinstance(requires, list) or not all(isinstance(item, str) for item in requires): raise ValueError("Invalid dependencies")
        if not isinstance(editors, list) or not all(item in ("level", "actor", "particle", "shader") for item in editors): raise ValueError("Invalid editors list")
        return {"id": identifier, "info": info, "path": path, "entry": entry, "requires": requires, "editors": editors, "enabled": False, "error": ""}
    def refresh(self):
        self.directory.mkdir(parents=True, exist_ok=True)
        present = set()
        for path in sorted(self.directory.iterdir()):
            if path.name.startswith("."): continue
            try:
                record = self._describe(path)
                if not record: continue
                key = record["id"]
                if key in present: raise ValueError(f"Duplicate add-on id: {key}")
                present.add(key)
                if not self.records.get(key, {}).get("enabled"): self.records[key] = record
            except Exception as error:
                log(f"Cannot discover {path.name}: {error}")
                key = "invalid_" + hashlib.sha256(str(path).encode()).hexdigest()[:12]
                present.add(key)
                self.records[key] = {"id": key, "path": path, "entry": path, "requires": [],
                    "editors": [], "info": {"name": path.name}, "enabled": False,
                    "invalid": True, "error": str(error)}
        for key in list(self.records):
            if key not in present:
                self._disable(key); del self.records[key]
    def enable(self, identifier, *, persist=True):
        record = self.records[identifier]
        if record["enabled"]: return
        if record.get("invalid"): raise ValueError(record["error"])
        if identifier in self._loading: raise ValueError("Circular add-on dependency: " + identifier)
        if context.editor not in record["editors"]: raise ValueError("Add-on does not support " + context.editor)
        previously_enabled = {key for key, value in self.records.items() if value["enabled"]}
        self._loading.add(identifier)
        try:
            for dependency in record["requires"]:
                if dependency not in self.records: raise ValueError("Missing dependency: " + dependency)
                self.enable(dependency, persist=False)
            name = "_xray_addon_" + identifier
            loader = importlib.machinery.SourceFileLoader(name, str(record["entry"]))
            spec = importlib.util.spec_from_file_location(name, record["entry"], loader=loader,
                submodule_search_locations=[str(record["entry"].parent)] if record["path"].is_dir() else None)
            module = importlib.util.module_from_spec(spec)
            sys.modules[name] = module; record["module"] = module
            def load():
                loader.exec_module(module)
                if not callable(getattr(module, "register", None)): raise ValueError("Add-on must define register()")
                module.register()
            ok, _ = _guard(load, identifier, budget=5)
            if not ok:
                self._disable(identifier, call_unregister=False)
                raise RuntimeError("Add-on registration failed: " + identifier)
            record.update(enabled=True, failures=0, error="")
            if persist: self._save()
        except BaseException:
            record["error"] = traceback.format_exc()
            for key in reversed(list(self.records)):
                if self.records[key]["enabled"] and key not in previously_enabled:
                    self._disable(key)
            raise
        finally: self._loading.discard(identifier)
    def _disable(self, identifier, call_unregister=True):
        record = self.records.get(identifier)
        if not record: return
        module = record.get("module")
        record["enabled"] = False
        if module and call_unregister and callable(getattr(module, "unregister", None)):
            _guard(module.unregister, identifier)
        _call("owner_remove", identifier)
        for collection in (_operators, _panels):
            for key in list(collection):
                if collection[key]["owner"] == identifier: del collection[key]
        _handlers[:] = [item for item in _handlers if item[3] != identifier]
        _timers[:] = [item for item in _timers if item[2] != identifier]
        for cls, item in list(_classes.items()):
            if item[1] == identifier: del _classes[cls]
        prefix = "_xray_addon_" + identifier
        for key in list(sys.modules):
            if key == prefix or key.startswith(prefix + "."): del sys.modules[key]
        record.pop("module", None)
    def disable(self, identifier):
        users = [key for key, value in self.records.items() if value["enabled"] and identifier in value["requires"]]
        if users: raise ValueError("Disable dependent add-ons first: " + ", ".join(users))
        self._disable(identifier); self._save()
    def reload(self, identifier):
        users = [key for key, value in self.records.items() if value["enabled"] and identifier in value["requires"]]
        if users: raise ValueError("Disable dependent add-ons before reloading: " + ", ".join(users))
        enabled = self.records[identifier]["enabled"]
        self._disable(identifier); self.refresh(); importlib.invalidate_caches()
        if enabled: self.enable(identifier)
    def uninstall(self, identifier):
        self.disable(identifier)
        path = self.records[identifier]["path"]
        trash = context.sdk_root / "settings" / "addon-trash"
        trash.mkdir(parents=True, exist_ok=True)
        path.rename(trash / (str(time.time_ns()) + "-" + path.name))
        del self.records[identifier]
    def install(self, filename):
        source = Path(filename)
        self.directory.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".install-", dir=self.directory) as stage_name:
            stage = Path(stage_name)
            if source.suffix.lower() == ".xpy":
                candidate = stage / source.name; shutil.copy2(source, candidate)
            elif source.suffix.lower() == ".zip":
                with zipfile.ZipFile(source) as archive:
                    members = archive.infolist()
                    if len(members) > 1024 or sum(item.file_size for item in members) > 32 * 1024 * 1024: raise ValueError("Add-on archive exceeds size limits")
                    for item in members:
                        relative = Path(item.filename.replace("\\", "/"))
                        target = (stage / relative).resolve()
                        if relative.is_absolute() or not target.is_relative_to(stage.resolve()) or (item.external_attr >> 16) & 0o170000 == 0o120000:
                            raise ValueError("Unsafe archive path")
                    archive.extractall(stage)
                if (stage / "addon.json").is_file(): candidate = stage
                else:
                    children = list(stage.iterdir())
                    if len(children) != 1: raise ValueError("ZIP must contain one add-on folder or one .xpy file")
                    candidate = children[0]
            else: raise ValueError("Expected .xpy or .zip")
            record = self._describe(candidate)
            if not record: raise ValueError("No add-on manifest found")
            identifier = record["id"]
            target = self.directory / (identifier if candidate.is_dir() else identifier + ".xpy")
            if identifier in self.records or target.exists(): raise ValueError("Add-on already exists; disable and uninstall it before installing a replacement")
            if candidate.is_dir(): shutil.copytree(candidate, target)
            else: shutil.copy2(candidate, target)
        self.refresh()
        log("Installed add-on: " + identifier)
        return identifier
    def create_template(self):
        index = 1
        while (self.directory / f"my_addon_{index}.xpy").exists(): index += 1
        path = self.directory / f"my_addon_{index}.xpy"
        path.write_text('addon_info = {"name": "My add-on", "api_version": 1}\nimport xray\n\ndef hello(context):\n    xray.log("Hello from", context.editor)\n\ndef register():\n    xray.operator("'+path.stem+'.hello", "Hello from my add-on", undo=False)(hello)\n\ndef unregister():\n    pass\n', encoding="utf-8")
        self.refresh(); os.startfile(path.parent); return path
    def open_folder(self): os.startfile(self.directory)


addons = _Addons()


def _invoke(identifier):
    entry = _operators[identifier]
    def execute():
        if entry["undo"] and context.editor == "level":
            with scene.transaction(entry["label"]): return entry["callback"](context)
        return entry["callback"](context)
    return _guard(execute, entry["owner"], budget=5)[1]


def _draw_panel(identifier):
    entry = _panels.get(identifier)
    if entry: _guard(entry["callback"], entry["owner"], context, budget=0.25)


def _event(name, payload):
    mapping = {"COMMAND_LOAD": "scene_loaded", "COMMAND_SAVE": "scene_saved", "COMMAND_CLEAR": "scene_cleared", "COMMAND_UNDO": "undo", "COMMAND_REDO": "redo"}
    names = [name]
    if name == "command_executed" and payload in mapping: names.append(mapping[payload])
    for token, event, callback, owner in list(_handlers):
        if event in names: _guard(callback, owner, payload, budget=0.25)


def _tick():
    global _selection, _selection_check
    now = time.monotonic()
    executed = 0
    for item in list(_timers):
        if executed >= 32: break
        if item not in _timers or item[3] > now: continue
        executed += 1
        ok, interval = _guard(item[1], item[2], budget=0.25)
        if item not in _timers: continue
        if not ok or interval is None: _timers.remove(item)
        elif not isinstance(interval, (float, int)) or not math.isfinite(interval) or interval < 0:
            log("Invalid timer interval"); _timers.remove(item)
        else:
            item[3] = now + max(0.01, interval)
            _timers.remove(item); _timers.append(item)
    if context.editor == "level" and now >= _selection_check and any(item[1] == "selection_changed" for item in _handlers):
        _selection_check = now + 0.1
        try: selected = _call("scene.selected")
        except RuntimeError: return
        if selected != _selection:
            _selection = selected; _event("selection_changed", selected)


def _draw_manager():
    for key, record in list(addons.records.items()):
        info = record["info"]
        ui.separator(); ui.text(f'{info.get("name", key)}  {info.get("version", "1.0")} [{key}]')
        ui.text(info.get("description", ""))
        ui.text("Enabled" if record["enabled"] else "Disabled")
        def action():
            if ui.button(("Disable" if record["enabled"] else "Enable") + "##" + key):
                addons.disable(key) if record["enabled"] else addons.enable(key)
            ui.same_line()
            if ui.button("Reload##" + key): addons.reload(key)
            ui.same_line()
            if ui.button("Uninstall##" + key): addons.uninstall(key)
        _guard(action, "console")
        if record.get("error"): ui.text(record["error"][-2000:])


def _console_exec(source):
    def execute():
        try: code = compile(source, "<SDK Console>", "eval")
        except SyntaxError: exec(compile(source, "<SDK Console>", "exec"), _console_globals)
        else:
            result = eval(code, _console_globals)
            if result is not None: log(repr(result))
    _guard(execute, "console", budget=5)


def _run_file(filename):
    path = Path(filename)
    if path.suffix.lower() != ".xpy": raise ValueError("Script file must have .xpy extension")
    _guard(lambda: exec(compile(path.read_text(encoding="utf-8-sig"), str(path), "exec"),
                        {"__name__": "__main__", "__file__": str(path), "xray": sys.modules[__name__]}), "console", budget=5)


def _start(root, editor):
    global _context, _console_globals
    _context = {"sdk_root": root, "editor": editor}
    sys.dont_write_bytecode = True
    sys.stdout = _Stream(); sys.stderr = _Stream()
    addons.directory = context.sdk_root / "addons"
    sys.meta_path.insert(0, _XpyFinder())
    _console_globals = {"xray": sys.modules[__name__], "context": context}
    addons.refresh()
    for identifier in addons._state().get(editor, []):
        if identifier in addons.records: _guard(lambda key=identifier: addons.enable(key, persist=False), "console", budget=8)
    _event("startup", editor)
    log(f"X-Ray Python {sys.version.split()[0]} | API {API_VERSION} | {editor} | .xpy add-ons")


def _shutdown():
    _event("shutdown", context.editor)
    for identifier in reversed(list(addons.records)): addons._disable(identifier)
    sys.stdout.flush(); sys.stderr.flush()
