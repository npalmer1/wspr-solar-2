# Making the Windows installer

The result is `installer\output\WSPR-Solar-2-<version>-setup.exe`. The version comes from
`project(WsprSolar VERSION ...)` in `CMakeLists.txt`.

## Once only

1. Install **Inno Setup** (free, version 7, or 6.3 or later): https://jrsoftware.org/isdl.php
2. Make the folder `installer\thirdparty` and copy into it the MySQL files that WSPR Solar 2
   already runs with (from your build folder):
   ```
   installer\thirdparty\sqldrivers\qsqlmysql.dll
   installer\thirdparty\libmysql.dll
   installer\thirdparty\libcrypto-3-x64.dll
   installer\thirdparty\libssl-3-x64.dll
   ```

`thirdparty`, `stage` and `output` are in `.gitignore`, so none of these go to GitHub.

## Each release

1. In Qt Creator, choose the **Release** build, then **Build**.
2. Double-click `installer\make_release.bat`.
3. Test `installer\output\WSPR-Solar-2-<version>-setup.exe`, then upload it to SourceForge.

The installer puts the program in `C:\Program Files\WSPR Solar 2` with a Start menu folder and,
if ticked, a desktop icon. It never touches the databases or DBcredential.
