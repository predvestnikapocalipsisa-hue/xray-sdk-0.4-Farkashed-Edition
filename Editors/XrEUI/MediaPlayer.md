# Title bar music controls

The shared editor title bar displays the current song and artist, with previous,
play/pause, stop and next buttons. Click the song area to choose a Windows media
session or return to automatic selection. The menu also opens Spotify, SoundCloud
and YouTube Music in the default browser. Start playback there first.

This uses Windows Global System Media Transport Controls (Windows 10 version
1809 or later), without service credentials or API keys. Desktop apps and browser
players appear only when they expose a system media session. Browser sessions may
be identified by the browser executable rather than the website. Available
commands depend on the player; unsupported buttons are disabled. This controls
existing playback and does not stream audio inside the editor.

The widget shares the editor theme and UI scale. Its width follows the measured
song/artist text and it centers itself between the editor caption and window
buttons. Long text is clipped to that free space and remains readable in the
tooltip. It hides when the title bar has insufficient space. The rest of the
title bar remains draggable. Media discovery,
metadata and commands run on a dedicated MTA worker with bounded async waits;
the worker is stopped before the editor UI is destroyed.

Only MediaPlayer.cpp requires C++17 and Windows SDK C++/WinRT headers. Its linker
directive adds windowsapp.lib to consumers of the XrEUI static library.

Manual validation: start music in an app or supported web player, verify Unicode
song/artist text, pause/resume and supported skip/stop actions, select a different
source, close the selected source, resize/scale/move the editor and close it while
music continues. Auto selection follows the current Windows media session.
