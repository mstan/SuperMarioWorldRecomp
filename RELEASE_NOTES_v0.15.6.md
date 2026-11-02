# Super Mario World 0.15.6

- Adds Netplay to the standard Super Mario World launcher and connects it to the shared account and lobby backend.
- Online play requires Discord sign-in. There is no guest fallback for online rooms. LAN / Direct IP remains available without signing in.
- Updates recomp-ui and recomp-net, including current lobby, account and ICE transport support.
- Keeps the native simultaneous co-op mod, adaptive widescreen, Falcon helper and opt-in Lua scripting in the standard package.

Open **Netplay → Online Netplay**, sign in with Discord, then choose **Host Lobby** and **Create**. Both players need this build. For simultaneous co-op, enable the native co-op mod and select **2 Player** in the original game menu.

Validation covers release builds, unsigned online connection rejection, and shared LAN hosting/rematch tests. Live Discord completion and an Internet-hosted lobby have not yet been verified in this pass.

Supply your own Super Mario World (USA) ROM. Preserve your existing `saves/`, `keybinds.ini`, configuration and `netplay_secret` when upgrading. Credentials and ROMs are not included in the downloads.
