addon_info = {"id": "native_smoke", "api_version": 1, "editors": ["level"]}
import json
import traceback
import xray


report = {"passed": False, "checks": []}


def finish():
    target = xray.context.sdk_root / "native-smoke.json"
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    xray.app.request_exit()


def check(condition, name):
    if not condition: raise AssertionError(name)
    report["checks"].append(name)


def register():
    try:
        check(xray.context.editor == "level", "embedded Python context")
        check("COMMAND_UNDO" in xray.ops.list(), "native command discovery")
        check(xray.lua.eval("2 + 3") == 5, "Python to Lua number")
        check(xray.lua.execute('return {name="test", values={1,2,3}}') == {"name": "test", "values": [1,2,3]}, "Lua table conversion")
        xray.lua.execute("function echo(value) return value end")
        check(xray.lua.call("echo", {"text": "Привет", "values": [1, 2]}) == {"text": "Привет", "values": [1, 2]}, "Python arguments to Lua function")
        xray.lua.execute("counter = 17")
        check(xray.lua.eval("counter") == 17, "Lua persistent owner state")
        with xray._owned("isolated_lua"):
            check(xray.lua.eval("counter") is None, "Lua owner isolation")
            xray.lua.reset()
        try: xray.lua.execute("while true do end")
        except RuntimeError as error: check("exceeded" in str(error), "Lua timeout")
        else: raise AssertionError("Lua loop was not interrupted")
        # Target changes are deliberately deferred by the editor until next frame.
        xray.ops.execute("COMMAND_CHANGE_TARGET", 4)
        xray.timers.register(scene_checks, first_interval=.05)
    except BaseException:
        report["error"] = traceback.format_exc()
        finish()
        raise


def scene_checks():
    try:
        baseline = len(xray.scene.objects)
        with xray.scene.transaction("Script integration smoke"):
            obj = xray.scene.create("shape", "xpy_smoke_shape", position=(1,2,3))
            obj.selected = True
            obj.scale = (2,3,4)
            obj.rotation = (0,.25,0)
            check(obj.position == (1,2,3), "native position")
            check(obj.scale == (2,3,4), "native scale")
            check(obj.selected, "native selection")
            obj.name = "xpy_smoke_renamed"
            check(obj.name == "xpy_smoke_renamed", "native rename")
        check(len(xray.scene.objects) == baseline + 1, "native creation")
        with xray.scene.transaction("Native command transaction"):
            xray.ops.execute("COMMAND_HIDE_ALL", False)
        check(not xray.scene.find("xpy_smoke_renamed").visible, "native command in transaction")
        check(xray.ops.execute("COMMAND_UNDO") == 1, "native command grouped Undo")
        check(xray.scene.find("xpy_smoke_renamed").visible, "native command Undo restored visibility")
        check(xray.ops.execute("COMMAND_UNDO") == 1, "single transaction Undo")
        check(len(xray.scene.objects) == baseline, "Undo restored scene")
        try: obj.position
        except RuntimeError: check(True, "stale handle rejected after Undo")
        else: raise AssertionError("Stale handle was accepted")
        check(xray.ops.execute("COMMAND_REDO") == 1, "Redo")
        obj = xray.scene.find("xpy_smoke_renamed")
        check(obj is not None, "find after Redo")
        try:
            with xray.scene.transaction("Rollback smoke"):
                obj.position = (100,100,100)
                raise ValueError("Deliberate rollback")
        except ValueError: pass
        obj = xray.scene.find("xpy_smoke_renamed")
        check(obj.position == (1,2,3), "exception rollback restored scene")
        obj.delete()
        check(xray.scene.find("xpy_smoke_renamed") is None, "native deletion")
        try: obj.position
        except RuntimeError: check(True, "deleted handle rejected")
        else: raise AssertionError("Deleted object was accessible")
        try: xray.ui.button("outside draw")
        except RuntimeError: check(True, "UI callback scope enforced")
        else: raise AssertionError("UI scope was not checked")
        report["passed"] = True
    except BaseException:
        report["error"] = traceback.format_exc()
    finally:
        finish()
    if not report["passed"]: raise RuntimeError(report.get("error", "Native smoke failed"))


def unregister():
    pass
