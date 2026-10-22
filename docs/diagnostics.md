# Crash and freeze diagnostics

Normal Windows launches write `skyrecomp.log` next to `skyrecomp.exe` and open a separate **SkyRecomp diagnostics** console that follows this file. Closing the diagnostic console does not close the game. It remains available after a crash so its output can be copied. The log is also available if the window was closed. `SKYRECOMP_CONSOLE=0` disables the extra console; logging remains enabled.

When a caller already redirects output to a file or pipe, that destination is preserved instead. Automated test runs therefore keep their own isolated logs.

A watchdog reports when no new game frame has been presented for 15 seconds. It records the last frame and guest thread states, then repeats at most every 30 seconds while the same stall continues. This is a **possible freeze** report: long loading or shader compilation can also trigger it. It does not terminate or restart the game. A busy kernel lock is reported rather than blocking the diagnostic thread.

If the game freezes, leave it for at least 15 seconds, then send `skyrecomp.log` together with the action, location, frame-rate setting and whether 4K Textures/ReShade were enabled. Crash reports include the fault address and Windows memory state.

The next normal launch starts a new log, so copy the report before launching again. No reports are uploaded automatically.
