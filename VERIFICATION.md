# Validation results — 2026-09-16

**Status: implementation and automated tests are complete; default isolated Explorer activation remains unresolved on the tested Windows 11 machine.** Do not interpret the passing renderer tests as proof that Explorer thumbnails currently work here.

## Passed

- MSVC x64 Debug and Release builds against pinned DirectXTex commit `4feb3e11a020f35b796fc769a74216a555d4f5ef`.
- CTest in both configurations: **1107 assertions** per run, including layouts, formats, transparency, color handling, limits, malformed input, stream behavior, and COM lifecycle.
- Generated 1D arrays, 2D mip chains, 2D arrays, volume maps, individual cubemaps, and cube arrays; correct mip 0 / array 0 / depth 0 selection.
- Labeled asymmetric cubemap fixture and its horizontal-cross PNG visually inspected.
- BC1–BC7, signed BC4/BC5 and BC6H, legacy RGB565 and luminance, BGRA, scalar/two-channel data, signed/unsigned integer normalization, HDR, typed depth/stencil, NV12, and supported typeless interpretations.
- Straight/premultiplied equivalence, opaque/custom alpha modes, checkerboard, transparent-edge filtering, explicit sRGB filtering, and nonfinite components.
- Direct DLL activation, COM identity, initialization errors, 100 repeated thumbnail requests, GDI orientation, and no observed GDI handle growth.
- Short stream reads, unknown stream size, truncated streams, read/seek failures, and rejection of oversized streams before reading or allocating their declared size.
- PowerShell 5.1 installation tests: registration, previous-handler backup, idempotent reinstall, uninstall restoration, COM/state cleanup, preservation of a subsequently changed handler, and preservation of the default application.
- Preview CLI produced the cubemap PNG and rejected a zero-size argument.
- Registered COM activation and explicit **in-process** Windows Shell extraction/cache-only retrieval produced the correct DDS pixels.
- Release DLL imports only Windows system DLLs; no separate MSVC or DirectXTex runtime DLL is required.

## Unresolved integration issue

The normal `IThumbnailCache` path returns `REGDB_E_CLASSNOTREG` (`0x80040154`) in the isolated thumbnail host. The effective `.dds` association resolves to this provider, and direct registered activation succeeds. Debugging showed the surrogate attempting to open this provider's CLSID and receiving `STATUS_OBJECT_NAME_NOT_FOUND` before the provider DLL was loaded. The underlying cause has not been established.

The same cache API succeeds with `WTS_EXTRACTINPROC`. That is a diagnostic result only: the installer does **not** set `DisableProcessIsolation`, and no in-process fallback is enabled for Explorer. An explicitly requested fresh surrogate also failed. A control test using Windows' existing photo thumbnail provider on a supported BC1 DDS succeeded; the original associations were restored after that test.

The temporary provider installation was removed after testing. The original per-user thumbnail registration state was restored, with no provider COM registration or installation state left behind. Debugger instrumentation was removed from the shipping DLL. Explorer was not terminated and the user's thumbnail cache was not cleared.

## Remaining acceptance work

1. Resolve and retest normal isolated activation on the current Windows 11 system.
2. Visually confirm actual Explorer folder thumbnails after isolated activation succeeds.
3. Run the package and Shell integration tests on Windows 10; Windows 10 compatibility is presently a target, not a runtime-verified result.

Reproduction commands and supported display rules are in [README.md](README.md). `tests/ShellTest.cpp` and `tests/Registration.ps1` provide repeatable integration checks without conflating them with renderer tests.
