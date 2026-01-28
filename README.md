# SyncTime

An AmigaOS 3.2+ commodity that synchronizes the system clock via SNTP (Simple Network Time Protocol) with full timezone and DST support.

## Features

- SNTP time synchronization from configurable NTP servers
- Full IANA timezone database with 400+ locations
- Region/city timezone picker with automatic DST handling
- Sets TZ and TZONE environment variables (POSIX format)
- Reaction-based GUI for easy configuration
- Scrollable activity log in separate window
- Standard commodity with Exchange integration
- Configurable sync interval (minimum 15 minutes)
- Automatic retry on network failures

## Requirements

- AmigaOS 3.2 or later (required for Reaction GUI)
- A TCP/IP stack with bsdsocket.library (Roadshow, AmiTCP, Miami, etc.)
- Network connectivity to an NTP server

## Installation

1. Copy `SyncTime` to your `SYS:WBStartup/` or `SYS:Tools/Commodities/` drawer
2. Optionally copy `SyncTime.info` for icon with tooltypes
3. Run SyncTime or reboot if placed in WBStartup

## Configuration

Configuration is stored in `ENV:SyncTime.prefs` and `ENVARC:SyncTime.prefs`.

Default settings:
- Server: `pool.ntp.org`
- Interval: `3600` seconds (1 hour)
- Timezone: `America/Los_Angeles`

## Tooltypes

SyncTime supports the standard Commodities Exchange tooltypes:

| Tooltype | Default | Description |
|----------|---------|-------------|
| `CX_PRIORITY` | `0` | Commodity priority (-128 to 127) |
| `CX_POPUP` | `NO` | Open configuration window on startup |
| `CX_POPKEY` | `ctrl alt s` | Hotkey to toggle the configuration window |

### Hotkey Format

The `CX_POPKEY` uses standard Commodities hotkey syntax:
- Qualifiers: `ctrl`, `alt`, `shift`, `lalt`, `ralt`, `lshift`, `rshift`, `lcommand`, `rcommand`
- Keys: Any key name (`a`-`z`, `0`-`9`, `f1`-`f10`, `help`, `del`, etc.)

Examples:
- `ctrl alt s` - Control + Alt + S
- `lcommand help` - Left Amiga + Help

## Usage

### From Workbench

Double-click the SyncTime icon. The commodity will start in the background and perform an initial time sync.

### From CLI

```
SyncTime
```

To run with tooltypes from CLI:
```
SyncTime CX_POPUP=YES
```

### Exchange Control

Once running, SyncTime appears in the Commodities Exchange:
- **Show Interface**: Opens the configuration window
- **Hide Interface**: Closes the configuration window
- **Remove**: Quits SyncTime

### Configuration Window

The window displays:

**Status Section:**
- **Status**: Current sync state (Idle, Syncing, Synchronized, or error messages)
- **Last sync**: Time of the last successful synchronization
- **Next sync**: Scheduled time for the next sync

**Settings Section:**
- **Server**: NTP server hostname (default: pool.ntp.org)
- **Interval**: Seconds between sync attempts (900-86400)

**Timezone Section:**
- **Region**: Geographic region (America, Europe, Asia, etc.)
- **City**: City within the selected region
- **Info**: Shows UTC offset and DST status for selected timezone

**Buttons:**
- **Sync Now**: Immediately perform a time synchronization
- **Save**: Apply changes and save to ENVARC:
- **Show Log / Hide Log**: Toggle the activity log window
- **Hide**: Close the configuration window

### Environment Variables

When you save configuration or on startup, SyncTime sets:
- **TZ**: POSIX-format timezone string (e.g., `UTC8DST7,M3.2.0,M11.1.0`)
- **TZONE**: Full IANA timezone name (e.g., `America/Los_Angeles`)

These variables are set globally and can be used by other timezone-aware applications.

### Retry Behavior

SyncTime syncs immediately on startup. If the sync fails (network unavailable, DNS error, etc.), it will automatically retry every 30 seconds until successful. Once a sync succeeds, it switches to the configured interval.

## Signals

- **CTRL+C**: Cleanly exits SyncTime

## Troubleshooting

### "DNS failed"
- Verify your TCP/IP stack is running
- Check that DNS is configured correctly
- Try using an IP address instead of hostname

### "Send failed"
- Check network connectivity
- Verify UDP port 123 is not blocked

### "No response"
- The NTP server may be unreachable
- Try a different server (e.g., `time.nist.gov`, `time.google.com`)

### "Bad response"
- The server sent an invalid NTP packet
- Try a different NTP server

## Building from Source

Requires the m68k-amigaos-gcc cross-compiler and Python 3 for timezone table generation.

```
make clean
make
```

The binary will be created as `SyncTime` in the project root.

## License

This software is released under the MIT License. See LICENSE file for details.

## Author

Nathan Ollerenshaw <chrome@stupendous.net>

## Version

1.0.0 - Initial release with IANA timezone database and Reaction GUI
