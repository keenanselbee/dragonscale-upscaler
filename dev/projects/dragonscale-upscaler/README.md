# DragonScale Upscaler

DragonScale is an SKSE/CommonLibSSE-NG plugin for Skyrim Special Edition focused on ENB-friendly temporal upscaling.

Current implementation order:

1. FSR2 upscaling path.
2. DLSS backend.
3. Backend cleanup and shared render plumbing.
4. ReShade under-UI integration.

Frame generation is intentionally outside the first implementation path.

## Build

The repo owns its local build tools under `dev/`. Bootstrap them once:

```powershell
powershell -ExecutionPolicy Bypass -File tools/bootstrap-dev.ps1
```

Build the SDK and plugin, then refresh the packaged mod DLL/PDB:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build.ps1 -RefreshMod
```

For rebuilds after the environment is already prepared:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build.ps1 -NoBootstrap -RefreshMod
```

The default INI path is:

```text
Data\SKSE\Plugins\dragonscale-upscaler.ini
```

## FidelityFX SDK

FSR2 D3D11 support uses the DX11-capable FidelityFX SDK source package. The top-level build script fetches the SDK when needed, applies DragonScale's DX11 FSR2 build patch, builds `ffx_backend_dx11_x64.lib` and `ffx_fsr2_x64.lib`, then builds the plugin.

## Compatibility Notes

SSE Display Tweaks is supported but not required. It is useful for borderless presentation, flip-model swapchains, frame limiting, and Havok timing, while DragonScale owns temporal render scaling inside Skyrim's render path.

For clean diagnostics, avoid enabling both DragonScale temporal scaling and SSE Display Tweaks window/resolution scaling at the same time. In SSE Display Tweaks, `BorderlessUpscale`, `Resolution`, and `ResolutionScale` are separate window/swapchain scaling features; DragonScale logs a warning when it detects those settings so zoom or extent mismatch reports are easier to interpret.
