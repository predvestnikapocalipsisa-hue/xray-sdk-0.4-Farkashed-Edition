# Editor crash reports

The Level Editor writes an exception report before attempting emergency scene
serialization. The shared runtime also reports unhandled exceptions, including
exceptions on worker threads. Reports use Win32 file operations independently
of the SDK log, filesystem, allocator and renderer.

Reports are local files under `crashes/` beside `Launcher.exe` (normally
`SDK/bins/crashes/`). If that directory cannot be created, the fallback is
`%TEMP%/XRaySDK-crashes/`. Nothing is uploaded. Each report has a `.txt` with
the exception code, address, module, module offset, process and thread IDs, and
a `.dmp` with thread contexts and module information. Preserve the matching
build's DLLs and PDBs to resolve the fault to source lines.

When investigating a report from a user, collect the matching `.txt` and `.dmp`,
the SDK version, and the last action if known. These files supplement the normal
SDK log. Reporting does not resume an editor after a hardware exception.
Forced termination and fail-fast exceptions can bypass in-process handlers;
stack overflows are left to Windows rather than writing on an exhausted stack.

## Regression checks

Build the SDK first, then build `tests/editor_lifetime_tests.vcxproj` with
MSBuild (`Release|x64`). Run `output/stability-tests/editor_lifetime_tests.exe`
with the built SDK directory on `PATH`. This exercises 2000 list replacements,
missing selection paths, and a selected item that becomes a folder.

For the real model-cache check, run the same executable from a configured SDK
root with an existing object name as its single argument, for example
`editor\camera`. It performs 1000 load/discard cycles with deferred GPU and
collision buffers, leaving the object asset unchanged.

Compile `tests/crash_report_tests.cpp` with MSVC (`/EHsc /std:c++14`) and run
it separately. It raises a test exception inside an explicit handler and checks
that a report and an `MDMP` dump are produced in a temporary test directory,
without initializing the SDK or displaying a crash dialog.
