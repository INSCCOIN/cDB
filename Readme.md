# cDB

Curses browser for the cScan SQLite file. Same green-on-black layout as cScan / cGotchi, built for a 480×320 Walnut panel.

Opens `/home/working/cScan/cscan.db` by default (read-only).

## Build

```bash
sudo apt install -y libncurses-dev libsqlite3-dev
gcc -O2 -Wall -Wextra -o cdb cDB.c -lncurses -lsqlite3
./cdb
./cdb /path/to/other.db
```

## Keys

| Key | |
|-----|--|
| `Enter` | Open table, or row INFO |
| `esc` / `b` | Back to table list |
| `/` | Filter rows (substring, any column) |
| `r` | Reload |
| `h` | Help |
| `↑` `↓` | Move (list scrolls) |
| `q` | Quit |

## Screens

**Tables** — name, row count, type (`networks`, `sightings`, `scans`, plus anything else in the file).

**Rows** — left pane is the useful pair (`ssid`/`bssid` or `ts` + next column). Right pane lists column names for the highlight. Enter dumps every column for that row.

Filter applies after load (up to 512 rows pulled per table).

Read-only. It will not write the database cScan is using.
