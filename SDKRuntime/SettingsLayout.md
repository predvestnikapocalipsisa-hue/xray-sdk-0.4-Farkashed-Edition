# SDK settings directory

Editor configuration can live in `<SDK>/settings/`. The filesystem loader first
looks for `settings/fs.ltx`, then the legacy `fs.ltx`, while walking up from the
working or binary directory. `$app_root$`, `$fs_root$` and `$sdk_root$` still refer
to the SDK root, so resource paths do not shift into the settings directory.

`$local_root$` in a migrated filesystem definition points to
`$sdk_root$ | settings\`. Editor preferences, ImGui layouts and Content Browser
state use that alias for both reading and writing. The converter also finds
`settings/fsgame.ltx` and resolves its `$fs_root$` against the SDK root.

Close the editors, install the matching SDKRuntime and LevelEditor binaries,
then run `OrganizeSettings.ps1 -SdkRoot 'E:\X-Ray SDK'` in PowerShell. The script
migrates only known editor configuration files, preserves their encodings,
checks existing destinations for conflicts, and verifies each new file before
moving originals into `bins/backups/settings-layout-<timestamp>/`.

Filesystem profiles remain individual editable LTX files. Compiler configuration
and game, InGame, project, and resource directories are outside this migration.
Themes in Documents/X-Ray SDK and launcher history in LocalAppData retain their
existing locations. Old SDK installations with root-level configs remain valid.
