"""Lifecycle tests run without opening an editor; native scene tests are separate."""
import ctypes
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import time
import unittest
import zipfile

API_FILE = Path(__file__).resolve().parents[1] / "python" / "xray" / "__init__.py"


class FakeNative:
    def __init__(self): self.registrations = {}; self.calls = []; self.logs = []
    def __call__(self, operation, text, x, y, z):
        operation, text = operation.decode(), text.decode()
        self.calls.append((operation, text))
        value = None
        if operation.startswith("register_"):
            identifier, label, owner, shortcut = text.split("\x1f")
            if identifier in self.registrations: return b'{"error":"Duplicate registration"}'
            self.registrations[identifier] = owner
        elif operation == "owner_remove":
            self.registrations = {key: owner for key, owner in self.registrations.items() if owner != text}
        elif operation == "log": self.logs.append(text)
        elif operation == "commands": value = ["COMMAND_UNDO"]
        elif operation.startswith("ui."): value = False
        return json.dumps({"ok": value}).encode()


class AddonTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.native = FakeNative()
        original = ctypes.PyDLL
        ctypes.PyDLL = lambda name: type("Library", (), {"SDKScriptCall": self.native})()
        try:
            spec = importlib.util.spec_from_file_location("xray", API_FILE, submodule_search_locations=[str(API_FILE.parent)])
            self.api = importlib.util.module_from_spec(spec)
            sys.modules["xray"] = self.api; spec.loader.exec_module(self.api)
        finally: ctypes.PyDLL = original
        self.api._context = {"sdk_root": str(self.root), "editor": "level"}
        self.api.addons.directory = self.root / "addons"
        self.api.addons.directory.mkdir()
        self.finder = self.api._XpyFinder(); sys.meta_path.insert(0, self.finder)
    def tearDown(self):
        for key in list(self.api.addons.records): self.api.addons._disable(key, call_unregister=False)
        sys.meta_path.remove(self.finder)
        sys.modules.pop("xray", None)
        self.temporary.cleanup()
    def addon(self, identifier, body, requires=None):
        path = self.api.addons.directory / identifier
        path.mkdir()
        (path / "addon.json").write_text(json.dumps({"id": identifier, "requires": requires or []}), encoding="utf-8")
        (path / "__init__.xpy").write_text(body, encoding="utf-8")
        return path
    def test_discovery_does_not_execute_disabled_addon(self):
        self.addon("disabled", 'raise RuntimeError("must not run")\n')
        self.api.addons.refresh()
        self.assertFalse(self.api.addons.records["disabled"]["enabled"])
    def test_relative_xpy_import_and_cleanup(self):
        path = self.addon("package", 'import xray\nfrom .helper import VALUE\ndef register():\n xray.operator("package.op", undo=False)(lambda ctx: VALUE)\n xray.panel("package.panel")(lambda ctx: None)\n xray.events.subscribe("test", lambda event: None)\n xray.timers.register(lambda: None)\n')
        (path / "helper.xpy").write_text("VALUE = 42", encoding="utf-8")
        self.api.addons.refresh(); self.api.addons.enable("package")
        self.assertEqual(self.api._invoke("package.op"), 42)
        self.api.addons.disable("package")
        self.assertFalse(self.native.registrations)
        self.assertFalse(self.api._handlers); self.assertFalse(self.api._timers)
        self.assertFalse(any(key.startswith("_xray_addon_package") for key in sys.modules))
    def test_registration_failure_cleans_partial_state(self):
        self.addon("broken", 'import xray\ndef register():\n xray.operator("broken.op")(lambda ctx: None)\n raise RuntimeError("broken")\n')
        self.api.addons.refresh()
        with self.assertRaises(RuntimeError): self.api.addons.enable("broken")
        self.assertFalse(self.native.registrations); self.assertFalse(self.api._operators)
    def test_dependency_failure_unloads_new_dependencies(self):
        self.addon("dependency", 'def register(): pass\n')
        self.addon("broken", 'def register(): raise ValueError("broken")\n', ["dependency"])
        self.api.addons.refresh()
        with self.assertRaises(RuntimeError): self.api.addons.enable("broken")
        self.assertFalse(any(value["enabled"] for value in self.api.addons.records.values()))
    def test_dependency_cycle_and_missing_dependency(self):
        self.addon("first", 'def register(): pass\n', ["second"])
        self.addon("second", 'def register(): pass\n', ["first"])
        self.api.addons.refresh()
        with self.assertRaises(ValueError): self.api.addons.enable("first")
        self.assertFalse(self.api.addons._loading)
    def test_repeated_callback_errors_disable_owner(self):
        self.addon("errors", 'import xray\ndef broken(ctx): raise RuntimeError("failed")\ndef register(): xray.operator("errors.op", undo=False)(broken)\n')
        self.api.addons.refresh(); self.api.addons.enable("errors")
        for _ in range(3): self.api._invoke("errors.op")
        self.assertFalse(self.api.addons.records["errors"]["enabled"])
        self.assertNotIn("errors.op", self.native.registrations)
    def test_reload_imports_fresh_code(self):
        path = self.addon("reloadable", 'import xray\ndef register(): xray.operator("reloadable.op", undo=False)(lambda ctx: 1)\n')
        self.api.addons.refresh(); self.api.addons.enable("reloadable")
        (path / "__init__.xpy").write_text('import xray\ndef register(): xray.operator("reloadable.op", undo=False)(lambda ctx: 1234)\n', encoding="utf-8")
        self.api.addons.reload("reloadable")
        self.assertEqual(self.api._invoke("reloadable.op"), 1234)
    def test_zip_traversal_is_rejected(self):
        archive = self.root / "unsafe.zip"
        with zipfile.ZipFile(archive, "w") as stream: stream.writestr("../escape.xpy", "pass")
        with self.assertRaises(ValueError): self.api.addons.install(archive)
        self.assertFalse((self.root / "escape.xpy").exists())
    def test_invalid_manifest_is_visible_but_cannot_execute(self):
        path = self.api.addons.directory / "invalid"
        path.mkdir(); (path / "addon.json").write_text("not JSON", encoding="utf-8")
        self.api.addons.refresh()
        key = next(iter(self.api.addons.records))
        self.assertTrue(self.api.addons.records[key]["invalid"])
        with self.assertRaises(ValueError): self.api.addons.enable(key)
    def test_install_and_recoverable_uninstall(self):
        source = self.root / "single.xpy"
        source.write_text('addon_info = {"id": "single"}\ndef register(): pass\n', encoding="utf-8")
        self.assertEqual(self.api.addons.install(source), "single")
        self.assertFalse(self.api.addons.records["single"]["enabled"])
        self.api.addons.uninstall("single")
        self.assertTrue(list((self.root / "settings" / "addon-trash").glob("*-single.xpy")))
    def test_nested_transaction_rolls_back_once(self):
        with self.assertRaises(ValueError):
            with self.api.scene.transaction():
                with self.api.scene.transaction(): raise ValueError("rollback")
        self.assertEqual([op for op, _ in self.native.calls if op.startswith("scene.")], ["scene.begin", "scene.rollback"])
    def test_timer_stops_and_timeout_is_caught(self):
        self.api.timers.register(lambda: None, 0)
        self.api._tick(); self.assertFalse(self.api._timers)
        def loop():
            while True: pass
        ok, _ = self.api._guard(loop, "console", budget=.01)
        self.assertFalse(ok)
    def test_future_timers_do_not_starve_ready_timer(self):
        values = []
        for _ in range(40): self.api.timers.register(lambda: None, 100)
        self.api.timers.register(lambda: values.append("ready"), 0)
        self.api._tick()
        self.assertEqual(values, ["ready"])


if __name__ == "__main__": unittest.main()
