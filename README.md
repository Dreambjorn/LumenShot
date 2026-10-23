# LumenShot

Portable Windows 11 x64 screenshot tool that turns HDR or SDR screens into correct sRGB PNGs.

## Use

- Run `LumenShot.exe` from a writable folder. It starts in the notification area.
- Double-click the tray icon for settings. Changes save automatically to `LumenShot.ini`.
- Press **Print Screen**, drag a region, annotate, then **Ctrl+S** to save or **Ctrl+C** to copy.
- **Esc** closes the capture. **Ctrl+Z** undoes a mark. **Ctrl+A** selects the whole captured area.
- Hover over a toolbar button for its label and available keyboard shortcut. Labels stay beside the button and within its monitor.
- In **Capture > Capture monitor**, choose **All monitors** to select across connected displays.
- Screenshots go to `Screenshots` beside the executable unless another folder is chosen.
- **Save HDR image** also exports an Ultra HDR JPEG or a lossless JPEG XR of the selection into `HDR images`.
- **Output > Sharpness** adds adaptive sharpening. It defaults to **Off**; start around **15%** for a subtle effect.
- **Output > Image size** offers **Original**, **125%**, **150%** and **200%**. Scaling and sharpening apply to PNG, clipboard and HDR exports.

## Notes

- All-monitor capture follows the Windows display arrangement, including gaps and negative coordinates. Each monitor retains its own HDR/SDR conversion.
- Displays are captured in succession, so moving content across screens can differ in time.
- The nits value is HDR reference white, not monitor peak brightness.
- At Original size with sharpness off, native SDR pixels retain their captured brightness and colors. HDR exposure affects HDR-to-SDR conversion only; normalized SDR content also bypasses that adjustment.
- HDR enhancement operates on floating-point luminance, preserving HDR brightness, wide colors and negative scRGB components. HDR images omit annotations; PNG and clipboard annotations follow the selected output size. Source files are read-only.
- If Print Screen is taken, close the other capture app or pick another shortcut.

## Build

- Needs CMake 3.25 or newer.
- `cmake --workflow --preset release` configures, builds and runs the tests.
- Output: `build/LumenShot.exe`.
- First run downloads GCC 15.3.0 and the codecs into a `tools` folder beside the project, verifying checksums. Set `HDRSHOT_TOOLS` to use another folder.

## License

- Original application code and artwork: [MIT License](LICENSE).
- Read all license texts through **General > Licenses**, the tray menu's **Licenses** command, or `LumenShot.exe --licenses`. They are embedded in the executable.
- Third-party components: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
