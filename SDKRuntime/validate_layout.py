"""Validate packaging invariants without invoking MSBuild or a compiler."""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
NS = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
solution = (ROOT / "xraySdkEditors.sln").read_text(encoding="utf-8-sig")
ET.parse(ROOT / "SdkPackaging.targets")
projects = {}
for name, relative, guid in re.findall(
        r'Project\("\{8BC9CEB8[^\n]+?= "([^"]+)", "([^"]+)", "([^"]+)"', solution):
    path = ROOT / relative.replace("\\", "/")
    projects[name] = (path, ET.parse(path).getroot(), guid)

applications = [name for name, (_, tree, _) in projects.items()
                if any(n.text == "Application" for n in tree.findall(".//m:ConfigurationType", NS))]
assert applications == ["Launcher"], applications
parts = {name for name, (_, tree, _) in projects.items()
         if tree.find(".//m:SdkRuntimePart", NS) is not None}
assert len(parts) == 15, parts
assert "<OutDir>$(SolutionDir)output\\libraries_$(Platform)_$(Configuration)\\</OutDir>" in (
    ROOT / "SdkPackaging.targets").read_text()
for name in parts:
    tree = projects[name][1]
    assert {n.text for n in tree.findall(".//m:ConfigurationType", NS)} == {"StaticLibrary"}, name
    for ref in tree.findall(".//m:ProjectReference", NS):
        assert ref.find("m:LinkLibraryDependencies", NS).text == "false", (name, ref.attrib)

graph = {}
for name, (path, tree, _) in projects.items():
    graph[name] = []
    for ref in tree.findall(".//m:ProjectReference", NS):
        target = (path.parent / ref.attrib["Include"].replace("\\", "/")).resolve()
        assert target.is_file(), (name, target)
        matching = [n for n, (p, _, _) in projects.items() if p.resolve() == target]
        assert len(matching) == 1, (name, target)
        graph[name] += matching
    for item in tree.findall(".//m:ClCompile[@Include]", NS):
        assert (path.parent / item.attrib["Include"].replace("\\", "/")).is_file(), (name, item.attrib)

visited = set()
def visit(name, active):
    assert name not in active, "Dependency cycle: " + " -> ".join(active + [name])
    if name in visited:
        return
    for child in graph[name]:
        visit(child, active + [name])
    visited.add(name)
for name in graph:
    visit(name, [])

# Shared sources must not introduce duplicate definitions in /WHOLEARCHIVE.
compiled = {}
for name in parts:
    path, tree, _ = projects[name]
    for item in tree.findall(".//m:ClCompile[@Include]", NS):
        excluded = item.find("m:ExcludedFromBuild", NS)
        if excluded is not None and excluded.text == "true":
            continue
        source = (path.parent / item.attrib["Include"].replace("\\", "/")).resolve()
        compiled.setdefault(source, []).append(name)
duplicates = {path.name: names for path, names in compiled.items() if len(names) > 1}
assert duplicates == {"xrSkin2W.cpp": ["XrCPU_Pipe", "XrECore"]} or duplicates == {
    "xrSkin2W.cpp": ["XrECore", "XrCPU_Pipe"]}, duplicates
for source_name, owner in [("xrXRC.cpp", "XrCDB"), ("tga.cpp", "XrDXT"),
                           ("PHNetState.cpp", "XrPhysics")]:
    actual = [name for path, names in compiled.items() if path.name == source_name for name in names]
    assert actual == [owner], (source_name, actual)

# Exporting the properties API within the merged runtime must not redefine the
# factory's SmartDynamicCast::smart_cast template into the dynamic_cast keyword.
props_prefix = (ROOT / "Editors/XrEProps/stdafx.h").read_text().split("#include", 1)[0]
def properties_defines(initial):
    macros = dict.fromkeys(initial, "1")
    stack = [True]
    for line in props_prefix.splitlines():
        if line.startswith("#ifdef "):
            stack.append(stack[-1] and line.split()[1] in macros)
        elif line.startswith("#if "):
            choices = re.findall(r"defined\((\w+)\)", line)
            stack.append(stack[-1] and any(name in macros for name in choices))
        elif line == "#else":
            stack[-1] = stack[-2] and not stack[-1]
        elif line == "#endif":
            stack.pop()
        elif line.startswith("#define ") and stack[-1]:
            _, name, value = line.split(maxsplit=2)
            macros[name] = value
    return macros
runtime_macros = properties_defines({"XR_SDK_RUNTIME_BUILD"})
assert "smart_cast" not in runtime_macros
assert runtime_macros["XREPROPS_API"] == "__declspec(dllexport)"
assert properties_defines({"XREPROPS_EXPORTS"})["smart_cast"] == "dynamic_cast"
assert properties_defines(set())["XREPROPS_API"] == "__declspec(dllimport)"
bugtrap = projects["BugTrap"][0].read_text(encoding="utf-8")
assert '$(SolutionDir)SdkPackaging.targets' in bugtrap
packaging = (ROOT / "SdkPackaging.targets").read_text()
for number in range(1, 5):
    assert f"xrSkin{number}W_x86=SdkCpuSkin{number}W_x86" in packaging

runtime_text = projects["SDKRuntime"][0].read_text(encoding="utf-8")
for part in parts:
    assert f"/WHOLEARCHIVE:{part}.lib" in runtime_text, part
    assert part in graph["SDKRuntime"], part
for editor in ["ActorEditor", "LevelEditor", "ParticleEditor", "ShaderEditor"]:
    path, tree, _ = projects[editor]
    assert {n.text for n in tree.findall(".//m:ConfigurationType", NS)} == {"DynamicLibrary"}
    expected = ["SDKRuntime", "FreeMagic"] if editor == "LevelEditor" else ["SDKRuntime"]
    assert graph[editor] == expected, (editor, graph[editor])
    if editor == "LevelEditor":
        free_magic = next(ref for ref in tree.findall(".//m:ProjectReference", NS)
                          if ref.attrib["Include"].replace("\\", "/") == "../FreeMagic/FreeMagic.vcxproj")
        assert free_magic.find("m:LinkLibraryDependencies", NS).text == "true"
    entry = path.parent / ("Kernel" if editor == "LevelEditor" else "") / (editor + ".cpp")
    text = entry.read_text(encoding="utf-8-sig", errors="replace")
    assert 'extern "C" int __cdecl SDKEditorMain()' in text
    assert "wWinMain" not in text

for name in ["SDKRuntime", "LauncherAssets"]:
    _, tree, guid = projects[name]
    assert len(tree.findall(".//m:ProjectConfiguration", NS)) == 6
    assert solution.count(guid + ".") == 12
resources = (ROOT / "LauncherAssets/LauncherAssets.rc").read_text()
ids = set()
for resource_id, resource_path in re.findall(r'(\w+)\s+(?:RCDATA|ICON)\s+"([^"]+)"', resources):
    assert resource_id not in ids
    ids.add(resource_id)
    assert (ROOT / "LauncherAssets" / resource_path).is_file(), resource_path
assert len(ids) == 10
assets_tree = projects["LauncherAssets"][1]
assert assets_tree.find(".//m:NoEntryPoint", NS).text == "true"
launcher = projects["Launcher"][0].read_text(encoding="utf-8")
assert "CopyLauncherAssets" not in launcher
assert "LauncherAsset Include" not in launcher
ui = (ROOT / "Launcher/Launcher.cpp").read_text(encoding="utf-8")
assert "AssetsDirectory" not in ui and "AddFontFromMemoryTTF" in ui
assert "IDR_LAUNCHER_LOGO" in ui
assert "float(logo->width)" in ui and "float(logo->height)" in ui
print(f"OK: {len(projects)} projects; 1 EXE; 6 SDK DLLs; {len(parts)} runtime archives; "
      f"acyclic dependencies; {len(ids)} embedded assets; 4 editor entry points.")
