# Installing and running Tailon Lock

Tailon Lock runs on macOS, Windows and Linux. You build it from source with a
few free tools, which takes about five minutes the first time. Pick your system
below and follow the steps in order.

| System | What you need | Command that starts the game |
|---|---|---|
| macOS 11 or later (Intel or Apple silicon) | Command Line Tools, CMake | `open build/macos-release/TailonLock.app` |
| Windows 10 or 11, 64-bit | Visual Studio Build Tools, CMake, Ninja | `build\windows-msvc\TailonLock.exe` |
| Linux, 64-bit | GCC or Clang, CMake, Ninja | `./build/linux-release/tailonlock` |

The game is all its own code: the build downloads nothing, and the program
uses only the libraries every system already has.

## 1. Get the source

With git, using the address from the repository page's **Code** button:

```sh
git clone <the repository's address>
cd <the folder it made>
```

Without git: on the repository's GitHub page, pick **Code > Download ZIP**,
unzip it, and open a terminal in the unzipped folder.

All the commands below are run from that folder.

## 2. macOS

**Requirements:** macOS 11 Big Sur or later, an Intel or Apple silicon Mac,
about 200 MB free for the build. Full Xcode is not needed.

1. Install Apple's compiler (once). A window opens; click **Install**:

   ```sh
   xcode-select --install
   ```

2. Install CMake (once). Either download the macOS installer from
   cmake.org and run it, or use Python's installer:

   ```sh
   pip3 install --user cmake
   ```

   If `cmake` is then not found, add pip's folder to your PATH. On most Macs
   it is `~/Library/Python/3.9/bin` (the number is your Python version):

   ```sh
   echo 'export PATH="$HOME/Library/Python/3.9/bin:$PATH"' >> ~/.zprofile
   source ~/.zprofile
   ```

3. Build the game:

   ```sh
   cmake --preset macos-release
   cmake --build --preset macos-release -j
   ```

4. (Optional) Run the tests:

   ```sh
   ctest --preset macos-release
   ```

5. Start the game:

   ```sh
   open build/macos-release/TailonLock.app
   ```

**Installing it like any other app:** drag `build/macos-release/TailonLock.app`
into your **Applications** folder. Everything the game needs is inside the app,
so it runs from anywhere. Then start it from Launchpad or Spotlight.

**If macOS refuses to open it.** A copy built on your own Mac opens normally.
A copy that was downloaded or copied from another computer is not signed by an
Apple developer, so macOS blocks it the first time. Right-click the app, pick
**Open**, and confirm. Or clear the download flag in Terminal:

```sh
xattr -dr com.apple.quarantine /Applications/TailonLock.app
```

**Seeing the game's log:** start it from Terminal instead:

```sh
build/macos-release/TailonLock.app/Contents/MacOS/TailonLock
```

**Settings** are kept in `~/Library/Application Support/TailonLock/`.

**Graphics:** on a Mac the Modern renderer uses Metal. Pick ULTRA in
**Settings > GRAPHICS** for the best picture; it runs smoothly on Apple silicon.

## 3. Windows

**Requirements:** Windows 10 or 11, 64-bit, and a graphics card with
Direct3D 12 (without one the game falls back to the Retro renderer,
which runs on the CPU).

1. Install the tools (once):
   - **Visual Studio 2022 Build Tools** (free, from Microsoft's Visual Studio
     download page). In the installer, tick **Desktop development with C++**.
     This gives you the compiler and also includes CMake and Ninja.
   - **Git for Windows**, only if you want to clone instead of downloading the ZIP.

   If you prefer winget, in PowerShell:

   ```powershell
   winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
   winget install Git.Git
   ```

2. Open the Start menu and run **x64 Native Tools Command Prompt for VS 2022**.
   It must be this prompt (not a plain Command Prompt or PowerShell), so the
   compiler is found. `cd` into the game's folder.

3. Build the game:

   ```bat
   cmake --preset windows-msvc
   cmake --build --preset windows-msvc
   ```

4. (Optional) Run the tests:

   ```bat
   ctest --preset windows-msvc
   ```

5. Start the game:

   ```bat
   build\windows-msvc\TailonLock.exe
   ```

**Installing it:** copy `TailonLock.exe` together with the `assets` folder next
to it (both in `build\windows-msvc\`) into a folder of your choice, for example
`C:\Games\TailonLock\`. The program and the `assets` folder must stay side by
side. Right-click the .exe and pick **Pin to Start** or **Send to > Desktop**
for a shortcut. The .exe has no DLLs to ship.

**If Windows warns "Windows protected your PC".** That is SmartScreen, which
warns about any program not signed by a known publisher. Click **More info**,
then **Run anyway**.

**Settings** are kept in `%APPDATA%\TailonLock\`.

**Building the Windows .exe on Linux instead:** see "Windows .exe from Linux"
in the [README](../README.md).

## 4. Linux

**Requirements:** a 64-bit distribution from the last few years, GCC 14 or
newer (or Clang), CMake 3.25 or newer, and a GPU driver with Vulkan for the
Modern renderer (without one the game uses the Retro renderer on the CPU).
Works on Wayland and X11.

1. Install the build tools. No other libraries are needed: the game opens the
   system's own Wayland or X11, sound (ALSA) and Vulkan libraries when it starts.

   Fedora, RHEL and other dnf-based systems:

   ```sh
   sudo dnf install gcc cmake ninja-build git
   ```

   Ubuntu, Debian, Linux Mint and other apt-based systems:

   ```sh
   sudo apt install build-essential cmake ninja-build git
   ```

   Arch and Manjaro:

   ```sh
   sudo pacman -S --needed base-devel cmake ninja git
   ```

   On other distributions, install the same tools under their own names.

   If your distribution's GCC is older than 14, use Clang instead with the
   `linux-clang` preset (a debug build), or install a newer GCC.

2. Build the game:

   ```sh
   cmake --preset linux-release
   cmake --build --preset linux-release
   ```

3. (Optional) Run the tests:

   ```sh
   ctest --preset linux-release
   ```

4. Start the game:

   ```sh
   ./build/linux-release/tailonlock
   ```

**Installing it:** copy the program and its `assets` folder (both in
`build/linux-release/`) to a folder of your choice, for example
`~/Games/tailonlock/`. They must stay side by side. Nothing else needs installing. For a menu entry, save this as
`~/.local/share/applications/tailonlock.desktop`, with the full path of your folder (the `~` shortcut does not work here):

```ini
[Desktop Entry]
Type=Application
Name=Tailon Lock
Exec=/full/path/to/Games/tailonlock/tailonlock
Path=/full/path/to/Games/tailonlock
Terminal=false
Categories=Game;
```

**Settings** are kept in `~/.config/tailonlock/` (or under `$XDG_CONFIG_HOME`).

**Gamepads:** if a pad is not seen, your user may need access to input
devices. Most desktop distributions handle this already; otherwise add your user
to the `input` group and log in again.

## 5. First start

The opening film plays first; any key or pad button skips it. Then the main
menu opens: START MISSION, HANGAR, SETTINGS, NETWORK, WATCH INTRO and EXIT.
Move with the arrow keys, change a value with Left and Right, and press Enter.

In flight, player 1 flies with W A S D, throttles with R and F, fires the cannon
with Space, a missile with Left Shift and flares with Q. Tab changes the view.
All the keys, the gamepad layout and two-player split screen are in
[PLAYING.md](PLAYING.md). Network play is in the README.

## 6. If something goes wrong

| Problem | What to do |
|---|---|
| The game starts with a black or broken picture | In **Settings**, set RENDERER to RETRO and restart. Then update your graphics driver. |
| It runs slowly | In **Settings > GRAPHICS**, pick LOW or MEDIUM. |
| No sound | Check that the system's output device is the one you expect, then restart the game. On Linux, check that PipeWire or PulseAudio is running. |
| Only one plain jet, no maps or film (the log says "no assets folder found") | The program and the `assets` folder must be in the same folder (on macOS, keep the app in one piece). |
| CMake says a preset is unknown | Your CMake is older than 3.25. Install a newer one. |
| CMake cannot find Ninja (Linux) | Install `ninja-build` (dnf and apt) or `ninja` (pacman). |
| Windows: `cl` is not found | Build from the **x64 Native Tools Command Prompt for VS 2022**, not a plain prompt. |
| You want to start over | Delete the `build` folder and run the build steps again. To reset your settings, delete the settings folder named above for your system. |

To uninstall, delete the game's folder (or the app on macOS) and its settings
folder.
