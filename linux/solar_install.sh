#!/usr/bin/env bash
# WSPR Solar 2 - install or update on Ubuntu / Debian / Raspberry Pi OS.
#
# Just this script is enough - download it and run it:
#   wget https://raw.githubusercontent.com/npalmer1/wspr-solar-2/main/linux/solar_install.sh
#   bash solar_install.sh
# (or, in a folder already cloned from GitHub:  bash linux/solar_install.sh)
#
# It asks for your password (sudo) where it needs it, and:
#   0. installs git if it isn't there, and gets the source: clones it into ~/wspr-solar-2, or if that's
#      already there, pulls the latest version - then carries on with the newest copy of itself;
#   1. installs what the program needs: Qt 6 and its MySQL driver, MariaDB (or keeps the MySQL already
#      there), fonts, the compiler and CMake;
#   2. builds WSPR Solar 2 and installs it in /usr/local (WsprSolar, with a menu entry);
#   3. starts MySQL now and whenever the PC starts, adds the "admin" database user and, if they aren't
#      there yet, the WSPR Scheduler databases (wspr_db.sql, from WSPR Scheduler 2) - existing databases
#      are never touched. If WSPR Scheduler 2 is already installed on this PC, all of that is there.
#
# Run it again to update: it pulls the latest version, rebuilds and reinstalls the program, and never
# changes the databases or settings that are already there.
#
# Options:
#   --dir DIR      keep the source in DIR instead of ~/wspr-solar-2
#   --no-update    don't clone or pull: build the source folder this script is in, as it is
#   --no-mysql     don't install or set up MySQL (the databases are on another PC)
#   --uninstall    remove the program again (not the source, databases or settings)
set -euo pipefail

REPO_URL="${WSPR_SOLAR_REPO:-https://github.com/npalmer1/wspr-solar-2.git}"
SQL_URL="https://raw.githubusercontent.com/npalmer1/wspr-scheduler-2/main/linux/wspr_db.sql"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX=/usr/local
WITH_MYSQL=1
UPDATE=1
UNINSTALL=0
DEST=""

say()  { printf '\n\033[1;34m== %s\033[0m\n' "$*"; }
ok()   { printf '\033[0;32m   %s\033[0m\n' "$*"; }
warn() { printf '\033[0;33m   ! %s\033[0m\n' "$*"; }
die()  { printf '\033[0;31m   %s\033[0m\n' "$*" >&2; exit 1; }

# Debian installed from a DVD / USB stick keeps that disc as a package source, and apt then stops at
# "Media change: please insert the disc ..." - hidden in this script's output, so it just looks hung.
# Turn those entries off (a copy of each file changed goes in /var/backups).
APT_ETC="${APT_ETC:-/etc/apt}"   # (tests point this elsewhere)
disable_cdrom_sources() {
    local f changed=0
    for f in "$APT_ETC/sources.list" "$APT_ETC"/sources.list.d/*.list; do
        [ -f "$f" ] && grep -q -E '^[[:space:]]*deb(-src)?[[:space:]].*cdrom:' "$f" || continue
        sudo cp "$f" "/var/backups/$(basename "$f").before-wspr"
        sudo sed -i -E 's/^([[:space:]]*deb(-src)?[[:space:]].*cdrom:)/# \1/' "$f"
        changed=1
    done
    for f in "$APT_ETC"/sources.list.d/*.sources; do
        [ -f "$f" ] && grep -q -E '^URIs:.*cdrom:' "$f" || continue
        grep -q -E '^Enabled:[[:space:]]*no' "$f" && continue
        sudo cp "$f" "/var/backups/$(basename "$f").before-wspr"
        sudo sed -i -E '/^URIs:.*cdrom:/a Enabled: no' "$f"
        changed=1
    done
    if [ "$changed" = 1 ]; then
        ok "Turned off the install DVD as a package source (packages now come from the internet)"
    fi
    return 0
}

# sudo apt-get ..., waiting (up to 10 minutes) if apt is busy - e.g. a fresh Debian / Raspberry Pi OS
# install runs its own software update in the background (packagekitd) as soon as the network is up
apt_get() {
    local log tries=0
    log=$(mktemp)
    while :; do
        if sudo apt-get -o DPkg::Lock::Timeout=600 "$@" 2>&1 | tee "$log"; [ "${PIPESTATUS[0]}" -eq 0 ]; then
            rm -f "$log"; return 0
        fi
        # "apt-get update" whose only error is the install DVD still listed as a package source (Debian
        # installed from DVD/USB): the internet sources were read, so carry on
        if [ "$1" = update ] && grep -q "cdrom:" "$log" \
           && ! grep -E "^(E|Err|Error):" "$log" | grep -q -v "cdrom:"; then
            warn "The install DVD is still listed as a package source - ignored. To stop the message, comment out"
            warn "the 'cdrom:' line in /etc/apt/sources.list (or add 'Enabled: no' to it in /etc/apt/sources.list.d/)"
            rm -f "$log"; return 0
        fi
        if grep -q -E "Could not get lock|Unable to lock" "$log" && [ $((tries += 1)) -le 60 ]; then
            [ "$tries" = 1 ] && warn "apt is busy (the system's own software update?) - waiting for it to finish..."
            sleep 10
            continue
        fi
        rm -f "$log"; return 1
    done
}

while [ $# -gt 0 ]; do
    case "$1" in
        --dir) [ $# -ge 2 ] || die "--dir needs a folder"; DEST="$2"; shift ;;
        --dir=*) DEST="${1#--dir=}" ;;
        --no-update) UPDATE=0 ;;
        --no-mysql) WITH_MYSQL=0 ;;
        --uninstall) UNINSTALL=1 ;;
        -h|--help) sed -n '2,/^set -e/{/^set -e/!p}' "$0"; exit 0 ;;
        *) die "Unknown option $1 (see --help)" ;;
    esac
    shift
done

[ "$(id -u)" -ne 0 ] || die "Run this as your normal user, not with sudo: it asks for the password itself."
command -v apt-get >/dev/null || die "This script is for Ubuntu / Debian / Raspberry Pi OS (apt). See linux/README.md."

# Where the source is: the folder this script is in, if it's a copy of WSPR Solar 2 (cloned or unpacked
# from a ZIP); otherwise --dir, or ~/wspr-solar-2
INSIDE=""
[ -f "$HERE/../CMakeLists.txt" ] && grep -q "WsprSolar" "$HERE/../CMakeLists.txt" && INSIDE="$(cd "$HERE/.." && pwd)"
if [ -z "$DEST" ]; then
    DEST="${INSIDE:-$HOME/wspr-solar-2}"
fi
DEST="$(mkdir -p "$(dirname "$DEST")" && cd "$(dirname "$DEST")" && pwd)/$(basename "$DEST")"

if [ "$UNINSTALL" = 1 ]; then
    BUILD="$DEST/build-linux-release"
    [ -f "$BUILD/install_manifest.txt" ] || die "Nothing to remove (no $BUILD/install_manifest.txt)."
    say "Removing WSPR Solar 2"
    xargs -d '\n' sudo rm -fv < "$BUILD/install_manifest.txt"
    ok "Removed. The source ($DEST), the databases and your settings are still there."
    exit 0
fi

# ---- 0. the source: git, then clone or pull ---------------------------------------------------------
if [ -z "${WSPR_SOLAR_BOOTSTRAPPED:-}" ] && [ "$UPDATE" = 1 ]; then
    say "Getting the latest WSPR Solar 2"
    if command -v git >/dev/null; then
        ok "git is installed"
    else
        disable_cdrom_sources
        apt_get update
        apt_get install -y git
        ok "git installed"
    fi
    clone() {
        if ! git clone "$REPO_URL" "$DEST"; then
            die "Couldn't download $REPO_URL - check the internet connection (and that the repository is public)."
        fi
        ok "Downloaded into $DEST"
    }
    if [ -d "$DEST/.git" ]; then
        cd "$DEST"
        if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
            warn "$DEST has changes of your own, so it wasn't updated:"
            git status --short --untracked-files=no | sed 's/^/     /'
            die "Keep them with 'git stash' (or 'git commit'), or throw them away with 'git checkout .', then run this again - or use --no-update to build it as it is."
        fi
        before="$(git rev-parse HEAD)"
        git pull --ff-only || die "git pull failed - see above. Or use --no-update to build the copy you have."
        if [ "$(git rev-parse HEAD)" = "$before" ]; then ok "$DEST is already the latest version"
        else ok "$DEST updated to the latest version"; fi
    elif [ -e "$DEST" ] && [ -n "$(ls -A "$DEST" 2>/dev/null)" ]; then
        # e.g. unpacked from a ZIP: keep it, but under another name, and clone a proper copy
        OLD="$DEST.old-$(date +%Y%m%d-%H%M%S)"
        mv "$DEST" "$OLD"
        warn "$DEST wasn't a git copy: moved it to $OLD"
        clone
    else
        rmdir "$DEST" 2>/dev/null || true
        clone
    fi
    # carry on with the newest copy of this script (it may have changed)
    export WSPR_SOLAR_BOOTSTRAPPED=1
    NEXT=(--dir "$DEST" --no-update)
    [ "$WITH_MYSQL" = 0 ] && NEXT+=(--no-mysql)
    exec bash "$DEST/linux/solar_install.sh" "${NEXT[@]}"
fi

SRC="$DEST"
[ -f "$SRC/CMakeLists.txt" ] || die "No WSPR Solar 2 source in $SRC."
BUILD="$SRC/build-linux-release"
cd "$SRC"

# ---- 1. packages ----------------------------------------------------------------------------------
say "Installing the packages WSPR Solar 2 needs (this can take a few minutes)"
disable_cdrom_sources
apt_get update   # first, so the package lists below are up to date
PKGS=(build-essential cmake git pkg-config wget
      qt6-base-dev libqt6sql6-mysql libxkbcommon-dev fonts-liberation openssl ca-certificates)
if [ "$WITH_MYSQL" = 1 ]; then
    # keep whichever this PC already has; otherwise MariaDB - the same on Ubuntu, Debian and Raspberry Pi OS,
    # and what XAMPP uses on Windows (the WSPR Scheduler programs work with MySQL too)
    mysql_state=$(dpkg-query -W -f='${Status}' mysql-server 2>/dev/null || true)
    case "$mysql_state" in
        "install ok installed") PKGS+=(mysql-server) ;;
        ""|"deinstall ok config-files"|"unknown ok not-installed") PKGS+=(mariadb-server) ;;
        *) die "MySQL is half installed or half removed ($mysql_state), so the database can't be set up.
   To remove it completely (this deletes any MySQL databases on this PC), then run this script again:
     sudo dpkg --purge --force-all mysql-server mysql-server-8.0
     sudo apt-get purge -y 'mysql-server*' 'mysql-client*' mysql-common
     sudo apt-get --fix-broken install -y && sudo apt-get autoremove -y
     sudo rm -rf /etc/mysql /var/lib/mysql" ;;
    esac
fi
apt_get install -y "${PKGS[@]}"
ok "Packages installed"

# ---- 2. build and install -------------------------------------------------------------------------
say "Building WSPR Solar 2"
cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" >/dev/null
cmake --build "$BUILD" -j"$(nproc)"
sudo cmake --install "$BUILD"
sudo update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
sudo gtk-update-icon-cache -q "$PREFIX/share/icons/hicolor" 2>/dev/null || true
ok "Installed $PREFIX/bin/WsprSolar (and 'WSPR Solar 2' in the applications menu)"

# ---- 3. MySQL ------------------------------------------------------------------------------------
if [ "$WITH_MYSQL" = 1 ]; then
    say "Setting up MySQL"
    UNIT=""
    for u in mysql mariadb; do
        if [ "$(systemctl show -p LoadState --value "$u" 2>/dev/null)" = loaded ]; then UNIT=$u; break; fi
    done
    [ -n "$UNIT" ] || die "No mysql or mariadb service found after installing - see linux/README.md."
    sudo systemctl enable --now "$UNIT"
    ok "$UNIT running, and starts with the PC"

    # the admin user the WSPR Scheduler programs log in as (root on Ubuntu logs in through sudo)
    if [ -n "$(sudo mysql -N -e "SELECT 1 FROM mysql.user WHERE user='admin' LIMIT 1")" ]; then
        ok "database user 'admin' already there (its password is not changed)"
    else
        sudo mysql <<SQL
CREATE USER IF NOT EXISTS 'admin'@'localhost' IDENTIFIED BY 'wspr';
CREATE USER IF NOT EXISTS 'admin'@'127.0.0.1' IDENTIFIED BY 'wspr';
GRANT ALL PRIVILEGES ON *.* TO 'admin'@'localhost' WITH GRANT OPTION;
GRANT ALL PRIVILEGES ON *.* TO 'admin'@'127.0.0.1' WITH GRANT OPTION;
FLUSH PRIVILEGES;
SQL
        ok "database user 'admin' added (password: wspr)"
    fi

    # the databases: only if they aren't there - never over existing ones. wspr_db.sql comes with
    # WSPR Scheduler 2: its copy on this PC if there is one, else downloaded from its GitHub page.
    # WSPR Solar 2 uses wspr_sol: with no WSPR Scheduler databases at all, the whole file is imported;
    # if they're there but wspr_sol isn't (an older wspr_db.sql), just wspr_sol's part of it.
    has_db() { [ -n "$(sudo mysql -N -e "SHOW DATABASES LIKE '$1'")" ]; }
    if has_db wspr_sol; then
        ok "WSPR Scheduler databases (and wspr_sol) already there (not changed)"
    else
        SQL_FILE=""
        SQL_NAME=""
        for f in "$HOME/wspr-scheduler-2/linux/wspr_db.sql" "$PREFIX/share/wspr-scheduler-2/wspr_db.sql" \
                 "$HOME/WSPR_Sked/wspr_db.sql" "$HOME/Downloads/wspr_db.sql"; do
            if [ -f "$f" ] && grep -q "Database: \`wspr_sol\`" "$f"; then SQL_FILE="$f"; SQL_NAME="$f"; break; fi
        done
        if [ -z "$SQL_FILE" ]; then
            TMP_SQL="$(mktemp --suffix=.sql)"
            if wget -q -O "$TMP_SQL" "$SQL_URL" && grep -q "Database: \`wspr_sol\`" "$TMP_SQL"; then
                SQL_FILE="$TMP_SQL"
                SQL_NAME="wspr_db.sql (downloaded)"
                ok "wspr_db.sql downloaded from WSPR Scheduler 2's GitHub page"
            fi
        fi
        if [ -z "$SQL_FILE" ]; then
            warn "wspr_db.sql couldn't be found or downloaded, so the wspr_sol database was not made."
            warn "Put wspr_db.sql (from WSPR Scheduler 2) in ~/Downloads and run this script again."
        elif has_db wspr; then
            # only wspr_sol's part: from its heading to the end, with the file's opening SET lines
            { sed -n '1,/^-- Database: /p' "$SQL_FILE" | sed '$d'; sed -n '/^-- Database: `wspr_sol`/,$p' "$SQL_FILE"; } \
                | sudo mysql
            if has_db wspr_sol; then ok "wspr_sol database made from $SQL_NAME (the others were already there)"
            else warn "wspr_sol couldn't be made from $SQL_NAME - see the messages above"; fi
        else
            sudo mysql < "$SQL_FILE"
            if has_db wspr_sol; then ok "WSPR Scheduler databases made from $SQL_NAME"
            else warn "$SQL_NAME was imported but there is no 'wspr_sol' database - is it the right file?"; fi
        fi
        rm -f "${TMP_SQL:-}"
    fi
fi

say "Done"
echo "   Start it from the applications menu (WSPR Solar 2), or type: WsprSolar"
echo "   It logs in to the database as 'admin', with the password WSPR Scheduler 2 saved (~/DBcredential),"
echo "   or 'wspr' if there isn't one."
