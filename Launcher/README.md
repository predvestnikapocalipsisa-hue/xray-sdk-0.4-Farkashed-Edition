# Farkashed SDK Launcher

Standalone Win32 / ImGui / DirectX 9 launcher using the visual design and artwork in
`assets/gui/Launcher.pdn`. It uses the repository's ImGui sources, with a standalone
configuration, and does not link to the SDK engine or editor DLLs.

The `Launcher` project is part of `xraySdkEditors.sln`, with the same six platform /
configuration combinations as the editors. Its output is `Launcher.exe` in
`BuildedSDK/<platform>_<configuration>/`. PNGs, icons and `font.otf` are embedded in
`LauncherAssets.dll`, loaded as read-only resources without extracting files.
The `.pdn` remains a design source;
Paint.NET is not a runtime dependency. The original artwork is preserved.
`assets/gui/Icon.ico` is embedded into the executable and used for both sizes of
the window icon, including the taskbar and Alt+Tab.

Place the launcher alongside `SDKRuntime.dll`, `LauncherAssets.dll`,
`ActorEditor.dll`, `LevelEditor.dll`, `ParticleEditor.dll` and `ShaderEditor.dll`,
plus the external runtime dependencies, or choose their directory in Settings.
The selected SDK must also contain its own `Launcher.exe`. The launcher starts
that executable with `--sdk-editor actor|level|particle|shader`; the child loads
the matching DLL and calls the exported `SDKEditorMain`. Dispatch happens before
the launcher UI or COM apartment is initialized, giving each editor its own SDK
globals, UI context and crash boundary. `Launcher.exe` is the only SDK executable.
Editor processes use that directory as their working directory, preserving SDK
`fs.ltx` and relative resource lookup. A missing editor is disabled and its tooltip
shows the expected path. The launcher remains open by default; minimizing after
launch is optional.

## Interface

Settings contains `DEBUG mode (log errors and continue)`. The saved setting
applies to newly launched editors through `-sdk_debug`, in any build configuration.
Failed SDK checks and error dialogs are logged with an `[SDK DEBUG]` prefix without
interrupting startup. Missing `fs.ltx` aliases, INI includes, sections and values
use fallbacks. Debug startup skips game rendering resources, character profiles
and particle libraries, and shows an empty viewport so the editor UI can be
debugged without the game data. The existing SDK log location is used; if it is
undefined, logs go to `_debug/logs/` beside the SDK modules. Editor/host DLLs and a
working Direct3D device are still required. Hardware exceptions and exhaustion
of memory cannot be continued.

Discord Rich Presence uses application ID `1542531211602169966`. Discord displays
that application's configured name as the title. The next line is `In Launcher`,
`In Level Editor`, `In Actor Editor`, `In Particle Editor` or `In Shader Editor`.
The final line contains the level/object filename without its extension, the
selected particle/group name, or the selected shader/material library item. Empty
editors show an explicit untitled/no-selection state; the launcher has no document
line. Document names are converted from the SDK's Windows code page to UTF-8.

Only one SDK process owns an RPC connection at a time. Open editors take priority
over the launcher, including while minimized or while another application has
focus. Focusing another editor transfers ownership to it; otherwise the current
editor keeps publishing document/selection changes. The launcher reconnects after
the last editor exits. Process-lifetime markers and an abandoned-mutex check also
allow recovery after an editor crash. The existing `logo` image key is used for
the new Discord application. RPC shuts down with its UI session.

- Red frame aligned to the window edge, original logo, editor icons, title-bar
  icons and OTF font. Icons are cropped to their visible bounds and area-filtered
  with alpha-aware colors at the current DPI, preserving their aspect ratio.
  Filtered icons are drawn at their exact physical size on integer pixel bounds,
  avoiding a second resampling pass and changing sharpness during press animations.
  The logo is also alpha-filtered at the physical display size while retaining
  its full design canvas, and is regenerated when monitor DPI changes.
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
`editor/actor.ini`, `level.ini` and `actor.ini` beside the SDK launcher. SDKs
with a custom `$local_root$` publish their entries when documents are next opened
or saved; this importer does not interpret arbitrary `fs.ltx` alias chains.

The launcher and history support Unicode. Editor document loading still uses the
SDK's ANSI paths: unsupported code-page characters and paths exceeding the SDK's
path buffer are rejected with a message, rather than silently substituted.

No build or runtime verification was performed while implementing this feature,
as requested. The project/manifest paths and changes were inspected statically.

## SDK build metadata

The root `version.json` is the source for the SDK version string. Before resource compilation, `Generate-BuildInfo.ps1` generates version-resource definitions and an embedded four-line record (version, build date, Git hash, branch) in the launcher intermediate directory. Editors read this record from the hosting Launcher.exe for About, using the embedded launcher logo. Unchanged inputs preserve generated timestamps. Rebuild the launcher with changed editor DLLs to refresh the SDK build date.
# SDK binary updates

The launcher Check updates button and the editors' About > Check Updates open an ImGui updater using the latest stable GitHub release from `predvestnikapocalipsisa-hue/xray-sdk-0.4-Farkashed-Edition`. Installed versions come from the target Launcher's embedded build information. Numeric comparison prevents downgrades (4.7 FE stays installed when GitHub has v4.6).

Download and install is an explicit action. The updater runs from a temporary copy, downloads the selected SDK release asset (not converter or source archives), displays progress and MB/s, validates archive paths, links, checksum when GitHub provides one, embedded version, required modules and architecture, then waits for editors and launchers to close. Windows `tar.exe` extracts ZIP/RAR/7z archives. Only EXE/DLL files beside the archived Launcher are installed; project files, libraries and settings are preserved. The worker backs up overwritten files and attempts rollback on installation failure, reporting the backup path if recovery is incomplete. New editor sessions are blocked during installation by `.sdk-update.lock`. Staging and backups remain in the temporary updater directory for recovery.
