#!/bin/sh
set -eu

prefix=${1:-/usr/local}
sysconfdir=${2:-/etc/uptime}
inittab=/etc/inittab
backup=/etc/inittab.uptime-console.bak
temporary=/etc/inittab.uptime-console.tmp
command="${prefix}/bin/uptime-console -c ${sysconfdir}/uptime.conf"
profile=/etc/profile
profile_backup=/etc/profile.uptime-console.bak
profile_temporary=/etc/profile.uptime-console.tmp

if [ "$(id -u)" -ne 0 ]; then
    echo "TTY1 setup requires root; install the binary and config manually." >&2
    exit 1
fi
if [ -f "$profile" ] && ! grep -F '# uptime-console interactive SSH launch' "$profile" >/dev/null 2>&1; then
    if [ ! -e "$profile_backup" ]; then cp -p "$profile" "$profile_backup"; fi
    cp -p "$profile" "$profile_temporary"
    {
        printf '\n# uptime-console interactive SSH launch\n'
        printf 'case $- in *i*) ;; *) return ;; esac\n'
        printf '[ -n "${SSH_TTY:-}" ] || return\n'
        printf '[ -t 0 ] && [ -t 1 ] || return\n'
        printf '%s\n' "$command"
    } >> "$profile_temporary"
    mv "$profile_temporary" "$profile"
    echo "Configured interactive SSH logins to open uptime-console. Original profile: $profile_backup"
elif [ -f "$profile" ]; then
    awk -v old="exec $command" -v command="$command" '$0 == old { print command; next } { print }' \
        "$profile" > "$profile_temporary"
    chmod 644 "$profile_temporary"
    chown root:root "$profile_temporary"
    mv "$profile_temporary" "$profile"
fi

if [ ! -f "$inittab" ]; then
    echo "Alpine inittab not found at $inittab; skipping TTY1 setup." >&2
    exit 0
fi
if grep -F "$command" "$inittab" >/dev/null 2>&1; then
    echo "uptime-console already owns TTY1."
    exit 0
fi

if [ ! -e "$backup" ]; then cp -p "$inittab" "$backup"; fi
awk -F: -v command="$command" '
    $1 == "tty1" {
        print "tty1::respawn:" command
        found = 1
        next
    }
    { print }
    END {
        if (!found) print "tty1::respawn:" command
    }
' "$inittab" > "$temporary"
chmod 644 "$temporary"
chown root:root "$temporary"
mv "$temporary" "$inittab"
if command -v init >/dev/null 2>&1; then init -q || true; fi
echo "Configured uptime-console to respawn on TTY1. Original inittab: $backup"