import xray
from .helpers import offset_position

distance = 1.0
selection_count = 0


class MoveSelected(xray.Operator):
    bl_idname = "scene_tools.move_up"
    bl_label = "Move selected up"
    bl_shortcut = "CTRL+SHIFT+U"

    def execute(self, context):
        objects = context.selected_objects
        for obj in objects:
            obj.position = offset_position(obj.position, distance)
        xray.log(f"Moved {len(objects)} object(s) by {distance}")
        return {"FINISHED"}


class SceneToolsPanel(xray.Panel):
    bl_idname = "scene_tools.panel"
    bl_label = "Scene Tools (.xpy)"

    def draw(self, context):
        global distance
        xray.ui.text(f"Selected: {selection_count}")
        distance = xray.ui.input_float("Offset", distance)
        xray.ui.operator("scene_tools.move_up")
        if xray.ui.button("Create box shape"):
            with xray.scene.transaction("Create shape"):
                obj = xray.scene.create("shape")
                obj.selected = True
        if xray.ui.button("Save preferences"):
            xray.preferences.save({"distance": distance})


def selection_changed(handles):
    global selection_count
    selection_count = len(handles)


def register():
    global distance
    distance = float(xray.preferences.load().get("distance", 1))
    xray.utils.register_class(MoveSelected)
    xray.utils.register_class(SceneToolsPanel)
    xray.events.subscribe("selection_changed", selection_changed)


def unregister():
    # Operators, panels, handlers, timers and imports are also cleaned up by the host.
    pass
