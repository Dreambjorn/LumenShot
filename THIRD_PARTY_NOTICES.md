# Third-party notices

All notices below and the full license texts in `licenses/` are embedded in the executable. Read them through **General > Licenses**, the tray menu's **Licenses** command, or `LumenShot.exe --licenses`. Third-party components retain their own licenses.

Built with WinLibs MinGW-w64 GCC 15.3.0 (UCRT). The executable statically links GCC/C++ and winpthreads runtime components; their notices accompany it:

- [GCC Runtime Library Exception 3.1](licenses/GCC-RUNTIME-EXCEPTION.txt)
- [GNU GPL version 3](licenses/GPL-3.0.txt)
- [GNU LGPL version 2.1](licenses/LGPL-2.1.txt)
- [MinGW-w64 runtime notices](licenses/MinGW-w64-runtime.txt)
- [MinGW-w64 package notices](licenses/MinGW-w64.txt)
- [MinGW-w64 overall license](licenses/MinGW-w64-ZPL.txt)
- [winpthreads notices](licenses/winpthreads.txt)

The HDR JPEG writer statically links unmodified [libultrahdr 2.0.2](https://github.com/google/libultrahdr/tree/v2.0.2) under its [MIT license](licenses/libultrahdr-MIT.txt), including image_io code under [Apache 2.0](licenses/Apache-2.0.txt) and base64 code under the [modp notice](licenses/modp-base64.txt). It also links unmodified [libjpeg-turbo 3.2.0](https://github.com/libjpeg-turbo/libjpeg-turbo/tree/3.2.0); see its [license overview](licenses/libjpeg-turbo-LICENSE.md) and [IJG README](licenses/libjpeg-turbo-README.ijg). This software is based in part on the work of the Independent JPEG Group.

This product includes Gain Map technology under license by Adobe. See the [Gain Map notice](licenses/Adobe-Gain-Map-NOTICE.txt) and Adobe's [Gain Map Specification Patent License](https://helpx.adobe.com/camera-raw/desktop/hdr-and-advanced-output/gain-map.html) for its terms.

Oklab transforms use Björn Ottosson's published matrices, public domain with MIT as an alternative: [source](https://bottosson.github.io/posts/oklab/#converting-from-linear-srgb-to-oklab).

The optional sharpener and scaler are CPU adaptations of AMD's [FidelityFX CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS/blob/master/ffx-cas/ffx_cas.h) sharpen-only and adaptive scaling paths, using linear-light sRGB, the per-channel filter and diagonal support. HDR uses luminance-based filtering to retain scRGB color ratios and values above SDR white. Sharpness blends the filtered result with the unsharpened result. At Original size, zero sharpness gives an exact bypass. Its [MIT notice](licenses/FidelityFX-CAS-MIT.txt) is also embedded in the executable.

The application icon is an original drawing. Windows APIs and codecs come with Windows and are not redistributed.
