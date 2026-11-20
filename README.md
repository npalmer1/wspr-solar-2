# WSPR Solar 2

Qt 6 (C++) port of the C# **WSPR Solar**, for Windows and Linux. It collects the solar and geomagnetic
conditions from NOAA's Space Weather Prediction Center, shows what they mean for HF propagation, and
keeps a day-by-day record in the `wspr_sol` database used by WSPR Scheduler.

- **Current data**: today's planetary and Boulder A and K indices, yesterday's solar flux (SFI), sunspot
  number and X-ray background; the NOAA scales (G geomagnetic storms, S radiation storms, R radio
  blackouts) for the last 3 hours and for the day; a summary of conditions; solar radio bursts.
- **Database**: the saved days - Ap/Kp/SFI/SSN, or proton flux / flares / radio bursts by 3-hour
  period - with date, Ap, SSN and flare-class filters.
- Links to NOAA, the Australian Space Weather Service, hamqsl.com and background reading. F1 opens
  the newest `About_WS_Solar_vN.rtf` (`About_WS_Solar_v5.rtf` is in this folder; the installer puts it
  next to the program; a newer one in `C:\WSPR_Sked` or `~/WSPR_Sked` would be used instead).

It updates on start-up and every 30 minutes, and checks NOAA can be reached every 5 minutes.

## Building

Qt 6.4 or later (Widgets, Sql with the MySQL driver, Network) and CMake. Open `CMakeLists.txt` in
Qt Creator, or:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Windows: put the MySQL driver files in `installer\thirdparty` (see `installer\README.md`) and every
build copies them next to `WsprSolar.exe`. `installer\make_release.bat` makes the setup program.

Linux (Ubuntu, Debian, Raspberry Pi OS):

```
sudo apt install build-essential cmake qt6-base-dev qt6-tools-dev libqt6sql6-mysql fonts-liberation
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
sudo cmake --install build          # or just run build/WsprSolar
```

Tests: `-DWSPRSOLAR_TESTS=ON`, then `build/solar_test` and (with `QT_QPA_PLATFORM=offscreen`)
`build/solarwindow_uitest`. They read NOAA files saved in `tests/data` and use scratch databases
(`wspr_sol_test`, `wspr_sol_uitest`), never the real `wspr_sol`.

## Linux

On Ubuntu, Debian or Raspberry Pi OS one script installs everything and builds the program:

```
wget https://raw.githubusercontent.com/npalmer1/wspr-solar-2/main/linux/solar_install.sh
bash solar_install.sh
```

Run it again to update. See [linux/README.md](linux/README.md).

## Differences from the C# version

- **A and K indices**: `AK.txt` comes from NOAA's FTP site, as in the C# version (Qt has no FTP, so
  WSPR Solar 2 has its own small FTP download, `ftpget.cpp`). If FTP doesn't work, it asks for the same
  file over https, and if that doesn't work either, uses NOAA's `daily-geomagnetic-indices.txt`. That
  file has no Boulder line, so the second row then shows **Fredericksburg** (another mid-latitude US
  station), and says so.
- **Database columns widened**: the proton-flux, flare and radio-burst columns (`pf00`..`s21`) are
  widened from 25 to 300 characters on start-up, if they are narrower. The C# version wrote up to 300
  characters, which MariaDB/MySQL in their default "strict" mode refuse for a 25-character column, so on
  busy days the row wasn't saved at all. Wider columns don't affect the C# version.
- **All the NOAA files are fetched at once**, in the background (C# fetched several of them more than
  once, one after another, and the window waited). A full update runs every 30 minutes rather than in
  parts spread over 25-45 minutes.
- **Values are saved only when they were read**: if a NOAA file can't be fetched, what is in the database
  is kept (C# could overwrite the day's proton flux and flares with blanks, or save the oldest day's
  radio bursts as today's when there were none).
- **SFI/SSN before NOAA's daily report** (about 02:45 UTC): the last saved values are shown (C# showed
  "unknown").
- Fixes, each covered by a test: September dates (.NET wrote "Sept", so September's indices weren't
  found); an SFI of exactly 70 rated "poor-fair" (C# gave no rating); an SSN of exactly 150 "Very high"
  (C# "--"); the highest storm, blackout and flare levels count (C# sometimes took the first or the last
  found); radio bursts over midnight; "Severe" sweep bursts counted; a proton flux of "0" no longer stops
  the PF/flare grid; the date filter together with the Ap/SSN filters (an SQL error in C#); values are
  passed to the database as parameters.
- **Forecast** shows hamqsl.com's picture (a click opens their page) without a web-browser control.
- The flare-class list is a drop-down, with a blank entry for "any class".

## Licence

GNU GPL v3 (see `LICENSE`).
