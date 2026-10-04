# Compact SDK package

`xraySdkEditors.sln` produces one SDK executable, `Launcher.exe`, and six SDK DLLs:

- `SDKRuntime.dll`: core, API, collision, physics, particles, CPU dispatch, audio,
  XML, shared editor UI, properties, tools, texture compression, entity factory
  and Luabind, which uses the core allocator.
- `ActorEditor.dll`, `LevelEditor.dll`, `ParticleEditor.dll`, `ShaderEditor.dll`:
  editor entry points and their editor-specific forms and commands.
- `LauncherAssets.dll`: read-only PNG, font and icon resources, with no DLL entry
  point and no executable initialization.

The old implementation projects remain in the solution for source organization,
PCH settings and parallel compilation. They produce intermediate `.lib` archives
under `output/libraries_<platform>_<configuration>`, not separate deployed DLLs.
`SDKRuntime` links their complete object contents with `/WHOLEARCHIVE`, retaining
the exported API required by the four editor modules. Archive project references
are build dependencies only, so libraries are not recursively copied into other
archives. The runtime explicitly references all required libraries and DLL imports.

`SdkPackaging.targets` applies a common `/MDd` or `/MD` CRT to combined code, selects
exports within the runtime and provides editor `.def` files with stable C entry
point names on both x86 and x64. Core and physics loader hooks run through the
single runtime `DllMain`. Duplicate physics serialization and skinning symbols
are handled explicitly; the CPU binder now resolves from `SDKRuntime.dll`.
XRC storage and constructors are linked only from XrCDB, and TGA serialization
only from XrDXT. The export switch does not enable editor-only cast macros in
entity-factory code. BugTrap shares zlib's CRT configuration.
LevelEditor also links FreeMagic statically for portal hull and plane fitting:
these functions are not exported by SDKRuntime. This adds no deployed DLL.

The launcher runs each editor in a child process of `Launcher.exe`; SDK global
state is not shared between simultaneous editors. Document arguments, working
directories, history updates and level crash recovery keep their original paths.
Editor window icons come from the editor DLL resources. Launching a DLL entry
point directly in the launcher UI process is deliberately unsupported.

Third-party DLLs remain separate: FreeImage, nvtt, Discord RPC, LuaJIT,
OpenAL and BugTrap. Their existing binary dependencies are not merged into SDK
code. Runtime copying is attached to `SDKRuntime`, which is reached when building
the launcher through its editor project references.

Use a clean output directory for the first build after migration. Existing editor
EXEs, old module DLLs and the previous `launcher/assets` folder in an already-built
SDK are not deleted automatically; no binaries or user data are removed by this
source change. Old executables are not compatible with the new intermediate libs.
The game-engine projects outside `xraySdkEditors.sln` are not part of this package.

Run `python SDKRuntime/validate_layout.py` for static project, resource, dependency
graph and entry-point checks. This does not compile, link or run any SDK binary.
Compiler/linker and runtime validation remain necessary before distributing a
build; no build was run during this migration, as requested.
