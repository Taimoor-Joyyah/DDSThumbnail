# DDS thumbnails for Windows Explorer

A Windows 11 / Windows 10 **x64 thumbnail provider**, with a CPU-only DirectXTex renderer and PNG preview CLI. No GPU
device, kernel driver, background service, or default-app replacement is required.

**Validation status:** renderer and COM tests pass, but normal isolated Shell activation is currently unresolved on the
tested Windows 11 system. See [VERIFICATION.md](VERIFICATION.md) before installing; the CLI works independently of that
Shell issue.

## Build

Use x64 MSVC (Visual Studio 2022 17.12+ or Visual Studio 2026), the Windows SDK, CMake 3.24+, and Git. The first
configure requires network access. CMake builds DirectXTex's May 2026 release, pinned to commit
`4feb3e11a020f35b796fc769a74216a555d4f5ef`, with matching static Debug/Release CRTs. The old `DirectXTex/include` and
`DirectXTex/lib` directory is unused.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix dist --component Runtime
cpack --config build/CPackConfig.cmake -C Release -B dist
```

For Visual Studio 2022 use generator `Visual Studio 17 2022`. CLion's MSVC/Ninja configuration also works. For an
offline build, provide the pinned source using `-DFETCHCONTENT_SOURCE_DIR_DIRECTXTEX=C:/path/to/DirectXTex`. Updating
the dependency means changing the pinned commit and rerunning the format and COM tests. Release packages include
DirectXTex's MIT license.

## Install / uninstall

Run from **x64 PowerShell as your normal user**; no administrator rights are needed:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\dist\Install.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\dist\Uninstall.ps1
```

The process-scoped execution-policy option does not modify your saved PowerShell policy. Installation copies files to
`%LOCALAPPDATA%\DDSThumbnail\1.0.0-<package-hash>`. Registration lives under
`HKCU\Software\Classes\CLSID\{9FB9E3A6-57EF-4A20-9377-2E8C546A09DB}` and
`.dds\shellex\{E357FCCD-A995-4576-B01F-234630154E96}`. Only thumbnail handling changes. The previous per-user handler is
saved in `registration.json` and restored only if this provider still owns the association. Machine-wide handlers are
revealed again when a per-user override is removed. Reinstalling retains the original association backup. Do not delete
`registration.json` manually.

The provider uses `IInitializeWithStream` and `IThumbnailProvider`, with apartment threading and Windows' normal
isolated thumbnail host. There is no `DisableProcessIsolation` override. Only install binaries built from trusted
source; the package is unsigned.

After installation open a DDS folder with **Large icons** or **Extra large icons**. Ensure Explorer's Folder Options >
View > **Always show icons, never thumbnails** is off. Existing entries can remain in Windows' thumbnail cache: use
Windows Disk Cleanup and select **Thumbnails** to regenerate that cache. This clears thumbnails system-wide for the
user, so the scripts do not do it automatically. Reopen the folder afterward. Windows may defer thumbnail generation on
remote/cloud files or according to system policy.

Loaded DLLs can remain locked in a Windows thumbnail host after uninstall. The scripts remove registration immediately,
retain cleanup state for locked files, and never terminate Explorer. Keep the original package, sign out and back in,
then run its `Uninstall.ps1` again. Versioned directories allow upgrades without overwriting a loaded DLL.

## Preview CLI

```powershell
.\dist\dds-preview.exe input.dds output.png --size 256
```

Size defaults to 256 and is capped at 1024. Existing output files are not overwritten. The CLI reports dimensions, DXGI
format numbers, array size, mip count, and selected subresources. It uses the same renderer as Explorer and is useful
for separating decoding issues from Shell registration/cache issues. Exit codes: 0 success, 1 processing/I/O failure, 2
invalid arguments or existing output.

## Display rules

Always **mip 0**, **array element 0**, and **volume depth 0**. DDS layers are array elements, not a separate
Photoshop-style layer stack. Volumes do not have a standard DDS array representation. Aspect ratio is retained without
enlarging ordinary 2D images. A 1D texture repeats its single row up to 16 pixels high.

Cubemaps show all six stored face images from cube 0 using the DirectXTex horizontal-cross convention, without rotations
or reflections:

```text
       +Y
   -X  +Z  +X  -Z
       -Y
```

Faces are resized individually before assembly. The canvas is 4 by 3 faces, with checkerboard in unused cells. Requested
cube sizes below 4 pixels return an unsupported error.

| Content                               | Display                                                                      |
|---------------------------------------|------------------------------------------------------------------------------|
| RGB/RGBA, BGRA, supported legacy DDS  | Ordinary color image                                                         |
| BC1/2/3/7                             | CPU decompression, then ordinary color                                       |
| BC4 / one channel                     | Grayscale                                                                    |
| BC5 / two channels                    | Red and green, blue zero; no guessed normal Z                                |
| Alpha-only A8                         | Opaque grayscale from alpha                                                  |
| SNORM                                 | Map -1..1 to 0..1                                                            |
| UINT / SINT                           | Normalize by full representable range (including packed 10:10:10:2)          |
| RGB floats, shared-exponent RGB, BC6H | Clamp negative RGB, fixed per-channel Reinhard `c/(1+c)`, then sRGB encoding |
| Scalar float / typed depth            | Grayscale clamped to 0..1; stencil ignored                                   |
| Two-channel floats                    | Clamp each channel to 0..1; blue zero                                        |
| Supported planar/video formats        | DirectXTex single-plane/RGB decoding                                         |
| Typeless                              | UNORM where available, otherwise FLOAT; BC6H interpreted as unsigned float   |

Typeless interpretation is a display convention, not recovered asset semantics. DDS generally cannot establish whether a
texture is a normal map, material data, or color. Untagged normalized RGB values are displayed directly. Explicit sRGB
is decoded before filtering and encoded once for display. HDR uses the same exposure/mapping for every cube face. No
automatic exposure or normal reconstruction is performed.

Transparency is composited over an 8-pixel gray checkerboard (192/224). Straight and premultiplied alpha metadata is
respected; unspecified alpha is treated as straight. Opaque/custom alpha modes ignore alpha as opacity. Filtering uses
premultiplied colors to avoid halos. Nonfinite input components become zero. The result is an opaque, top-down 32-bit
BGRA DIB.

Unsupported formats (including typeless formats without a supported interpretation), partial legacy cubemaps, malformed
headers, and truncated payloads return errors. Explorer then retains its ordinary file icon. Support follows the pinned
DirectXTex reader and conversion routines, not every possible DXGI resource/view type or proprietary DDS extension.

## Limits and testing

The renderer loads the whole DDS container, but decompresses/converts only the selected image or cube faces. Limits: 256
MiB input, 512 MiB conservatively estimated working memory, 16384 pixels per 2D dimension, depth 2048, 12288 array
items, 15 mips, and 65536 image descriptors. These are thumbnail limits, not a claim that every otherwise-valid DDS can
be previewed. Large mip-0 textures may be rejected even if their compressed file is small. All allocations and size
calculations are checked before decoding; decoding errors do not escape across COM.

`dds-tests` generates fixtures and PNG previews in the build's `fixtures` directory. It checks subresource selection,
cube orientation, format/channel behavior, transparency, malformed files, size limits, direct DLL activation, COM
identity/lifetime, repeated thumbnail calls, and GDI handle cleanup. No registration is needed for these tests. Shell
integration requires separate installation tests on each target OS; build/test success alone is not proof of Explorer or
Windows 10 behavior.

Explicit integration checks (these are not part of automatic CTest):

```powershell
# Run while the provider is uninstalled; the test restores the original association.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\Registration.ps1
# After installing, request extraction and then a cache-only read of the red fixture.
.\build\Release\dds-shell-test.exe .\build\fixtures\2d-mip0.dds
```

`dds-shell-test --inproc` is a diagnostic option, supplied after the filename. It requests in-process extraction for
that test call only and does not change registration or disable Explorer's process isolation. `--fresh-surrogate`
requests an isolated host explicitly. The test expects a red fixture and checks its cached pixels as well as successful
API calls.

Future extensions: middle/three-slice volume previews, normal-map reconstruction, exposure controls, alternate cube
layouts, selective DDS subresource reads, and ARM64 builds.

## Upstream references

- [Windows thumbnail interface](https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/nn-thumbcache-ithumbnailprovider)
- [DirectXTex pinned release](https://github.com/microsoft/DirectXTex/releases/tag/may2026)
- [DirectXTex horizontal cross implementation](https://github.com/microsoft/DirectXTex/blob/may2026/Texassemble/texassemble.cpp)
