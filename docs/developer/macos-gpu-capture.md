# On-demand Metal frame diagnostics

Draw traces record guest state and resource identities, but not the pixels produced by a
render pass. A successful shader compilation and draw submission do not establish that
the translated shader or its resources are correct. For visual defects without reported
failures, capture a GPU frame before changing rendering behavior.

## Diagnostic package

Use a separate app identifier and `EglantineProfile`. Copy the user's configuration and
validate compatible shader caches with `tools/macos/seed_shader_cache.py`. Preserve the
original app and profile. Add these values to the diagnostic app's `LSEnvironment`:

```text
MTL_CAPTURE_ENABLED=1
MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE=3
MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE=/absolute/isolated/profile/dump/black-sky.gputrace
EDEN_FRAME_TRACE=1
```

Set `MetalCaptureEnabled` to true in the app's `Info.plist`, then sign the package again.
Capture must be enabled at process launch. Do not modify a running app or inject settings
into its process. Capture instrumentation can affect performance; use the ordinary build
for performance comparisons.

The user starts the diagnostic app and reproduces the visual defect. Identify that app's
PID, then request one frame:

```sh
python3 tools/macos/capture_gpu_frame.py --app /absolute/Eglantine.app --pid PID
```

The helper verifies the executable, isolated capture destination, and process-owned
MoltenVK FIFO. It writes one byte, which requests one frame at presentation boundaries.
It also requests the existing bounded guest draw-trace burst. Those traces are nearby
frames, not guaranteed to coincide with the exact Metal frame. The helper neither starts
nor stops gameplay. It refuses another request until the existing capture and request
receipt are archived.

## Inspecting the result

Allow rendering to continue for a few seconds. The request receipt only records that the
request was sent; it does not prove capture completion. Verify that the `.gputrace`
package has a readable `metadata` property list, a nonzero captured frame count, and
stable files before opening it in Xcode's Metal debugger. Preserve the accompanying
emulator log, native shader dumps, draw traces, and exact diagnostic app.

Inspect the render-target history for a black sky pixel. Find the first incorrect pass,
then inspect its shader, bindings, texture contents, depth state, and color outputs.
Use that evidence to narrow the shader translation or resource handling defect and add
a focused regression test. Do not use a replacement sky color, disable a pass, or clear
all caches as a substitute for identifying the fault.

References:

- [Apple: Capturing a Metal workload programmatically](https://developer.apple.com/documentation/xcode/capturing-a-metal-workload-programmatically)
- [MoltenVK capture configuration](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Configuration_Parameters.md)
