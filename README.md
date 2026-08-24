<img src="assets/AG.png" width="140" align="right" alt="ARMGDDN Browser" />

ARMGDDN Browser
===============


<p align="center">
  <img src="https://github.com/user-attachments/assets/a39de7ef-c5af-4b3e-9666-f984eeba4edc" alt="ARMGDDN Browser" width="700">
</p>

A lean, download-focused GUI for browsing and pulling from [rclone](https://rclone.org/)
remotes ("mirrors"), built for Windows.

ARMGDDN Browser is a fork of [Rclone Browser](https://github.com/kapitainsky/RcloneBrowser)
by kapitainsky (originally by Martins Mozeiko), stripped down and reworked with ❤️
by **DMP of ARMGDDN Games**. The config editor, mounting, streaming, uploads and
the tasks / queue / scheduler system are gone — what's left is a simple browser
plus downloads.

What's New
----------

<!-- HIGHLIGHTS:START -->
_Latest release: **v0.0.8**_

## Highlights



-
<!-- HIGHLIGHTS:END -->

Links
-----

* **Download:** https://github.com/DeliciousMeatPop/ARMGDDNBrowser
* **Beta site:** https://ARMGDDNBrowser.com
* **ARMGDDN Games Telegram:** https://t.me/ARMGDDNGames
* **DMP:** https://t.me/SickSoThr33 · **Old Man:** https://t.me/George_jefferson

(These are also in the app's **Links** menu.)

Features
--------

* **Self-contained & portable.** The app always uses `AG.exe`/`rclone.exe` and
  `ag.conf`/`rclone.conf` found next to the executable — nothing is
  user-configurable and no paths are exposed. A blank `ARMGDDNBrowser.ini`
  beside the exe enables portable mode.
* **Mirrors tab.** Your remotes are listed as *Mirrors*. The Google Drive
  remote type shows the ARMGDDN logo.
* **Mirror folders.** Group remotes under collapsible folders by name pattern
  (see [Configuration](#configuration)). Grouped mirrors are shown in
  black-and-white and ungrouped ones in colour, so the folder boundary is
  obvious. Grouping is display-only — the rclone config is never touched.
* **Search inside a remote.** A search box filters the mirror's game folders by
  name. It searches one level down (the folders inside each top-level wrapper,
  e.g. the games under `PC3`) — never the whole tree — so it stays fast and
  unintrusive. It waits until you stop typing, computes matches in the
  background so the UI never freezes, then offers a **Show N results** button to
  apply them.
* **Steam links.** Right-click a game folder to open its **Steam Store**,
  **SteamDB** and **Patchnotes** pages. The app reads the numeric `appid` file
  shipped inside the folder and the build id from the folder name
  (`Game vBUILDID -ARMGDDN`); if there's no `appid` file it isn't a Steam game
  and the links are greyed out.
* **Folder sizes.** Folders show the size of their contents, computed lazily in
  the background so browsing stays responsive.
* **Simple downloads.** Clicking **Download** just asks for the destination and
  the three options that matter — **Transfers**, **Checkers** and **Bandwidth**.
  Every other rclone option comes from the ini.
* **Check Local Files Against Server.** Right-click a folder to verify an
  existing local copy: it downloads into the folder you pick, so matching files
  are skipped (verified) and missing/changed ones are (re)downloaded.
* **Quota-aware.** If a download hits a quota / rate-limit error, the app finds
  the sibling mirrors in the same folder and offers to retry from one of them.
* **Live jobs.** Downloads open expanded with live progress (collapse them with
  the arrow); the header percentage stays current without opening the raw log.
* **Config check on start.** Optionally runs `ARMGDDNBrowser.cmd` (next to the
  app) before opening, so a fresh `ag.conf` can be pulled down first. **Refresh**
  also re-detects the binary/config, so a config that appears later is picked up.
* **Update check with one-click install.** Optionally checks this repo's
  releases for a newer ARMGDDN Browser. When one is found you can **Download &
  Install** it in place — the app fetches the release zip, closes, runs a small
  updater that replaces everything **except your `ARMGDDNBrowser.ini`**, and
  relaunches automatically — or open the releases page to grab it manually.
* **Version in the title bar.** The window title shows the running version
  (`ARMGDDN Browser vX.Y.Z`), matching the About dialog.

Configuration
-------------

Settings live in `ARMGDDNBrowser.ini` next to the exe (portable mode). Most
options are edited in the file directly; only the download folder and the
Transfers/Checkers/Bandwidth values are exposed in the GUI.

### Mirror folders

Define folders under a `[RemoteFolders]` group as `FolderName=pattern`, where
`*` is a wildcard. Text after a `/` is an **exclude** term.

```ini
[RemoteFolders]
Titles=TO-*          ; remotes whose name starts with TO-
HD=*hd*              ; remotes whose name contains hd
4K=*4k               ; remotes whose name ends with 4k
Stuff=Stuff-* / ftp  ; the Stuff-* mirrors, but not any whose name contains ftp
```

### Useful settings keys

```ini
[Settings]
defaultDownloadDir=D:\Downloads    ; default download folder
defaultDownloadOptions=            ; extra rclone options for downloads
defaultRcloneOptions=--fast-list   ; extra rclone options for all operations
checkRcloneUpdates=true            ; run ARMGDDNBrowser.cmd (config check) on start
checkRcloneBrowserUpdates=true     ; check this repo for a newer release
```

Build (Windows)
---------------

Requires Qt 6 (with the Multimedia module) and CMake.

```cmd
mkdir build && cd build
cmake -A x64 -DCMAKE_PREFIX_PATH="%QT_ROOT_DIR%" ..
cmake --build . --config Release
"%QT_ROOT_DIR%\bin\windeployqt.exe" --no-translations --no-compiler-runtime --no-svg .\build\Release\ARMGDDNBrowser.exe
```

Releases are produced by the **Build ARMGDDN Browser** GitHub Action: run it with
a version number and it builds a single x64 Windows package, tags `vX.X.X`, and
creates a draft release with `ARMGDDN.Browser.vX.X.X.zip` (an `ARMGDDN Browser`
folder containing the app and a blank `ARMGDDNBrowser.ini`).

License & credits
-----------------

ARMGDDN Browser is released under the MIT License (see [LICENSE](LICENSE)).

It builds on the work of the Rclone Browser authors — kapitainsky and
Martins Mozeiko — and, of course, [rclone](https://rclone.org/) itself.
