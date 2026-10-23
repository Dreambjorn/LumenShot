# LumenShot

Portable Windows 11 x64 screenshot tool that saves HDR or SDR screens as correct sRGB PNGs.

## Use

- Run `LumenShot.exe` from a writable folder; it lives in the notification area.
- Double-click the tray icon for settings, saved to `LumenShot.ini`.
- **Print Screen** to capture, drag a region, then **Ctrl+S** to save or **Ctrl+C** to copy.
- **Esc** closes, **Ctrl+Z** undoes, **Ctrl+A** selects all. Hover toolbar buttons for shortcuts.
- Screenshots go to `Screenshots` beside the executable unless changed.
- **Save HDR image** also exports Ultra HDR JPEG or JPEG XR into `HDR images`.
- If Print Screen is taken, close the other capture app or pick another shortcut.

## Build

- Needs CMake 3.25+. GCC and the codecs are downloaded on the first build.
- They are stored in `tools` beside the project; set `LUMENSHOT_TOOLS` to use another folder.
- Run `cmake --workflow --preset release` to configure, build and test.
- Output: `build/LumenShot.exe`.

## License

- MIT: [LICENSE](LICENSE). Third-party: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- All texts are embedded: **General > Licenses** or `LumenShot.exe --licenses`.
