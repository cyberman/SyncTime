# SyncTime

An AmigaOS 3.2+ commodity that synchronizes the system clock via SNTP with full timezone and DST support.

## Features

- SNTP time synchronization from configurable NTP servers
- Full IANA timezone database with 400+ locations
- Region/city timezone picker with automatic DST handling
- Sets TZ and TZONE environment variables
- Reaction-based GUI for easy configuration
- Scrollable activity log
- Standard commodity with Exchange integration
- Runs quietly in the background

## Requirements

- AmigaOS 3.2 or later
- TCP/IP stack with bsdsocket.library (Roadshow, AmiTCP, Miami, etc.)
- Network connectivity

## Installation

Copy `SyncTime` to `SYS:WBStartup/` or `SYS:Tools/Commodities/`.

Configuration is stored in `ENVARC:SyncTime.prefs`.

## Usage

SyncTime runs as a standard Amiga commodity. Use Exchange to show/hide the window, or press the hotkey (default: `ctrl alt s`).

From the configuration window you can:
- View sync status and last/next sync times
- Configure the NTP server (default: pool.ntp.org)
- Set the sync interval (900-86400 seconds)
- Select your timezone by region and city
- View the activity log
- Trigger an immediate sync

## Tooltypes

| Tooltype | Default | Description |
|----------|---------|-------------|
| `CX_PRIORITY` | `0` | Commodity priority |
| `CX_POPUP` | `NO` | Open window on startup |
| `CX_POPKEY` | `ctrl alt s` | Hotkey to toggle window |
| `DONOTWAIT` | - | Workbench won't wait for program to exit (recommended for WBStartup) |

## Environment Variables

On save/startup, SyncTime sets:
- **TZ**: POSIX timezone string (e.g., `PST8PDT,M3.2.0,M11.1.0`)
- **TZONE**: IANA timezone name (e.g., `America/Los_Angeles`)

## Building

Requires m68k-amigaos-gcc cross-compiler and Python 3.

```
make clean && make
```

## License

MIT License. See LICENSE file.

## Author

Nathan Ollerenshaw <chrome@stupendous.net>

## Source

https://github.com/matjam/synctime
