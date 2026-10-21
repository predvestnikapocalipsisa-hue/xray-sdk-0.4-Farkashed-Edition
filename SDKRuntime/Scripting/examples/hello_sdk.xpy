addon_info = {
    "id": "hello_sdk",
    "name": "Hello SDK",
    "version": "1.0.0",
    "api_version": 1,
    "description": "A minimal single-file .xpy add-on for all editors."
}
import xray


def hello(context):
    xray.log("Hello from", context.editor, "— API", xray.app.api_version)


def register():
    xray.operator("hello_sdk.hello", "Hello SDK", undo=False)(hello)


def unregister():
    pass
