# Farkashed SDK Launcher

Standalone Win32 / ImGui / DirectX 9 launcher using the visual design and artwork in
`assets/gui/Launcher.pdn`. It uses the repository's ImGui sources, with a standalone
configuration, and does not link to the SDK engine or editor DLLs.

The `Launcher` project is part of `xraySdkEditors.sln`, with the same six platform /
configuration combinations as the editors. Its output is `Launcher.exe` in
`BuildedSDK/<platform>_<configuration>/`. Its project copies the PNGs and `font.otf`
to `launcher/assets/gui/` beside the executable. The `.pdn` remains a design source;
Paint.NET is not a runtime dependency. The original artwork is preserved.
`assets/gui/Icon.ico` is embedded into the executable and used for both sizes of
the window icon, including the taskbar and Alt+Tab.

Place the launcher alongside `ActorEditor.exe`, `LevelEditor.exe`,
`ParticleEditor.exe` and `ShaderEditor.exe`, or choose their directory in Settings.
Editor processes use that directory as their working directory, preserving SDK
`fs.ltx` and relative resource lookup. A missing editor is disabled and its tooltip
shows the expected path. The launcher remains open by default; minimizing after
launch is optional.

## Interface

- Red frame aligned to the window edge, original logo, editor icons, title-bar
  icons and OTF font. Icons are cropped to their visible bounds and area-filtered
  with alpha-aware colors at the current DPI, preserving their aspect ratio.
  Filtered icons are drawn at their exact physical size on integer pixel bounds,
  avoiding a second resampling pass and changing sharpness during press animations.
- Smooth hover / press transitions, entrance / close fade, keyboard navigation and
  an optional reduced-motion setting.
- Borderless draggable title bar, minimize / close controls and per-monitor DPI.
- Live procedural background blur through Windows composition, using the system
  backdrop on supported Windows 11 versions and a dynamically resolved accent
  blur policy on Windows 10. No desktop screenshot is cached. Blur can be disabled
  and the background shade adjusted; unavailable composition falls back to a solid
  dark background. A move-loop timer keeps the interface animations rendering
  while the window is dragged.
- Open-file dialog and file drag-and-drop; recent-file rows with full-path tooltips,
  missing-file indicators, Explorer reveal and history removal.
- DirectX device-loss recovery and no continuous rendering while minimized.

## Documents and recent history

Supported documents are `.level` scenes, `.object` objects and binary particle
`.xr` libraries. Shader/material/animation `.xr` files are not passed to the
particle loader; those libraries are managed within Shader Editor. Shader Editor
has its own launch button.

The launcher passes a quoted `--launcher-open <absolute path>` argument. Level,
Actor and Particle editors consume it **after** their main form is constructed.
Level Editor also creates the corresponding scene tab. The original command line
is parsed with `CommandLineToArgvW`, preserving spaces and case.

Level and Actor editors publish successful file loads/saves; Particle Editor
publishes successful `.xr` imports to the shared history. A started
process or failed document load does not create a new entry. Each entry retains
its editor and SDK executable directory, so different SDK installations can be
used from the same launcher. A per-session named mutex serializes history access.
Up to 25 entries and launcher preferences are kept in the UTF-16 file:

`%LOCALAPPDATA%/FarkashedSDK/Launcher.ini`

Existing recent entries are imported once per installation from `editor/level.ini`,
`editor/actor.ini`, `level.ini` and `actor.ini` beside the editor executables. SDKs
with a custom `$local_root$` publish their entries when documents are next opened
or saved; this importer does not interpret arbitrary `fs.ltx` alias chains.

The launcher and history support Unicode. Editor document loading still uses the
SDK's ANSI paths: unsupported code-page characters and paths exceeding the SDK's
path buffer are rejected with a message, rather than silently substituted.

No build or runtime verification was performed while implementing this feature,
as requested. The project/manifest paths and changes were inspected statically.
