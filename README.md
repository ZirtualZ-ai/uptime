# Uptime

Uptime is a small C11/ncurses operations console for a homelab station. It runs in a physical terminal or an SSH terminal and keeps network work in a background pthread so the display remains responsive.

## Features

- Seven keyboard-operated views: overview, servers, services, discovery, device history, events, and configuration.
- Local IPv4, uptime, load average, and memory use from Linux interfaces and `/proc`.
- Configurable ping and key-authenticated SSH checks, with CPU, memory, disk, uptime, and optional thermal readings.
- Nmap host discovery only (`-sn`); the sole target is the configured IPv4 CIDR. No port scans are launched.
- Device first-seen/last-seen, MAC, manufacturer (when Nmap can obtain it), online state, and scan history in SQLite.
- TCP and HTTP/HTTPS service checks, response times, failure counts, and transition events.
- UDP Wake-on-LAN magic packets; sending a packet is reported separately from confirming that the host booted.
- Automatic watchdog recovery: three consecutive missed pings confirm an outage, followed by WOL retries no more than once every five minutes. A discovered target IP and MAC are retained in SQLite for later wake attempts.
- Versioned SQLite schema, 15-minute health samples plus state transitions, and automatic retention of old telemetry.

Configuration is an INI-style file. The configuration view is read-only; edit the file and restart Uptime to apply changes.

## Alpine Linux 3.24 (x86 / i686)

The Alpine v3.24 package index lists `build-base`, `ncurses-dev`, `sqlite-dev`, `nmap`, `openssh-client-default`, and `curl` for the `x86` architecture. Alpine calls the 32-bit target `x86`; confirm the installed image with `apk --print-arch`.

Install build and runtime dependencies as root:

```sh
apk add --no-cache build-base ncurses-dev sqlite-dev nmap openssh-client-default curl
```

Build natively on the Alpine x86 installation:

```sh
make clean
make
make check
make install
vi /etc/uptime/uptime.conf
reboot
```

On Alpine, `make install` creates `/etc/uptime/uptime.conf` from the example when no config exists, creates `/var/lib/uptime` for the database, and replaces the TTY1 `respawn` entry in `/etc/inittab` with `uptime-console`. It backs up the original file at `/etc/inittab.uptime-console.bak`; TTY1 will launch the console at boot and restart it if it exits. Review the installed config and network subnet before rebooting. Nmap, SSH client, and curl must be installed separately as listed above. Other Linux distributions receive the binary and config but no automatic TTY takeover.

To rebuild with debug symbols on the Alpine machine and replace the installed executable:

```sh
make debug
make install
/usr/local/bin/uptime-console --version
```

The version should report `Uptime 0.1.2`. In GDB, set breakpoints on the `pthread_create` call in `main` and `worker_main` and verify the `state` pointer immediately before creation matches the worker's `argument`.

Start it manually from a terminal when not using the Alpine TTY1 setup:

```sh
/usr/local/bin/uptime-console -c /etc/uptime/uptime.conf
```

The `ncurses-dev` and `sqlite-dev` packages are the development packages; `openssh-client-default` is the Alpine package providing the SSH client (there is no package named `openssh-client` in the checked v3.24 x86 index). Alpine's base BusyBox supplies `ping`.

For an optional local TTY1 login launch, add this to the interactive user's `~/.profile` after testing the command manually:

```sh
if [ "$(tty)" = /dev/tty1 ] && [ -z "${SSH_TTY:-}" ]; then
    exec /usr/local/bin/uptime-console -c /etc/uptime/uptime.conf
fi
```

## Debian Development

Install native build dependencies and compile for the current Debian architecture:

```sh
sudo apt update
sudo apt install build-essential libncurses-dev libsqlite3-dev
make clean
make
make check
```

That produces a binary for the Debian host, not automatically for i686. To produce a 32-bit x86 binary on Debian, enable i386 multiarch and install 32-bit development libraries:

```sh
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install gcc-multilib libc6-dev-i386 libncurses-dev:i386 libsqlite3-dev:i386
make clean
make CC=gcc CFLAGS='-O2 -m32 -std=c11 -Wall -Wextra -Wpedantic -pthread' LDFLAGS='-m32'
file uptime-console
```

For the most reliable Alpine target, build directly on the laptop's Alpine x86 installation using the preceding Alpine instructions. This Debian workspace build was validated on amd64; an Alpine/i686 runtime is not available in the development environment.

## Configuration

Start from [`uptime.conf.example`](uptime.conf.example). It includes the requested `b460m` host and Website, Ollama, Open WebUI, AI gateway, Minecraft, and SSH service examples. Review the subnet and all targets before enabling scans.

```ini
[global]
subnet = 192.168.40.0/24
scan_interval = 300
monitor_interval = 60
database = /var/lib/uptime/uptime.db
scan_enabled = true

[server b460m]
address = 192.168.40.10
user = marshal
port = 22
wol_mac = aa:bb:cc:dd:ee:ff
broadcast = 192.168.40.255

[service ollama]
server = b460m
type = tcp
target = 192.168.40.10
port = 11434
interval = 30
```

The supported sections are `[global]`, `[server NAME]`, and `[service NAME]`. Global settings are `subnet`, `scan_interval` (30 to 86400 seconds), `monitor_interval` (10 to 3600 seconds), `database`, and `scan_enabled`. Network scanning defaults to off when no config is loaded; the example enables it for its explicitly configured `/24`.

Server settings are `address`, `user`, `port`, and optional `wol_mac`/`broadcast`. SSH uses `BatchMode=yes`, a connection timeout, and the user's SSH keys; no password is requested or stored. Set up and test key access as the monitoring account, for example `ssh -o BatchMode=yes -p 22 marshal@192.168.40.10 true`.

Service settings are `server`, `type` (`tcp`, `http`, or `https`), `target`, `port`, optional `expected_status`, and `interval`. TCP targets are hostnames or IPs and use `port`. HTTP(S) targets are complete URLs; the URL determines the actual HTTP port. `expected_status` defaults to 200; set it to `0` to accept any 2xx or 3xx response. HTTP checks use `curl` with certificate verification enabled.

Validate or generate a config without entering the UI:

```sh
uptime-console --check-config /etc/uptime/uptime.conf
uptime-console --init-config /tmp/uptime.conf
uptime-console --init-db /tmp/uptime.db
```

The database is created on first launch. Health samples older than 30 days, events older than 180 days, and scan records older than one year are pruned on startup. Known device records are retained.

## Keyboard Controls

| Key | Action |
| --- | --- |
| `1` | Overview |
| `2` | Servers |
| `3` | Services |
| `4` | Network discovery |
| `5` | Device history |
| `6` | Events |
| `7` | Configuration |
| `Up` / `Down`, `k` / `j` | Select a row |
| `W` | Send Wake-on-LAN to the selected server on the Servers view |
| `R` | Queue a network discovery scan |
| `Q` | Quit |

Nmap scans run on the worker thread and have a 120-second timeout. Server and service checks share that background worker; a long scan can delay their next scheduled check, but does not block terminal input. Server statistics use one SSH connection per configured monitoring interval, with a one-second remote CPU sampling window. Health history is persisted on first observation, state changes, and at most every 15 minutes while stable to reduce disk writes.

## Project Layout

- `src/main.c`: startup, options, and thread lifecycle.
- `src/ui.c`: ncurses screens and keyboard navigation.
- `src/config.c`: INI parsing and validation.
- `src/worker.c`: periodic server, service, and Nmap work.
- `src/process.c`: bounded subprocess capture and TCP probes.
- `src/db.c`: SQLite schema, migrations, history, and device records.
- `src/local.c`: local Linux statistics.
- `src/wol.c`: validated UDP magic-packet sender.
- `Makefile`: build, check, install, and clean targets.