# Custom Discord RPC

Open **Customization → Discord RPC** in any SDK editor. Enable **Use custom activity**, edit the fields, then click **Apply & Save**. **Enable Discord RPC** controls publishing for the launcher and every editor. **Defaults** restores the draft; click Apply to save it.

The Application ID is always `1542531211602169966`. The activity name is editable independently. Name, details and state support `{environment}` (current editor or launcher) and `{document}` (current file/object). Empty optional fields are omitted. Text is UTF-8; expanded fields are shortened without splitting a character.

Images accept this application's uploaded asset keys or external HTTPS image URLs. Two optional buttons accept a label and HTTP(S) URL. Discord shows these buttons to other users, not their owner. Timers can be hidden, show elapsed process session time, or use explicit Unix timestamps in seconds. Advanced fields publish party and invite metadata; the SDK does not handle join or spectate commands.

Settings are stored per Windows user in `Documents\X-Ray SDK\discord.cfg`, separately from `theme.cfg`. Writes replace the configuration atomically. Running SDK processes reload valid settings once per second. Invalid or incomplete files leave the last valid configuration active. The RPC tab reports the connection state and Discord rejection responses.

The transport uses Windows named pipes `discord-ipc-0` through `discord-ipc-9`, protocol v1 and `SET_ACTIVITY`. It handles fragmented frames, ping/pong, disconnects and automatic reconnect. Pipe reads and writes use overlapped I/O. The existing publisher mutex and editor markers preserve foreground-editor priority over the launcher and other editors.

Protocol references: [Discord RPC framing](https://github.com/discord/discord-rpc/blob/master/documentation/hard-mode.md), [custom names and Rich Presence fields](https://discord.com/developers/discord-social-sdk/development-guides/setting-rich-presence).

Build `Launcher\tests\discord_rpc_tests.vcxproj` with Release/x64, then run `output\Tests\DiscordRPC\discord_rpc_tests.exe`. This uses an isolated mock pipe and does not change Discord activity or user settings. It checks settings round trips and rejection, JSON escaping, UTF-8 truncation, fixed-ID handshake, custom-name payload, fragmented messages, ping/pong and reconnect/resend.

The optional `--live` switch connects to the running Discord desktop client, briefly publishes a test activity, verifies the custom name in Discord's reply, and clears it.
