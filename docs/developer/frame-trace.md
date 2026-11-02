# Vulkan frame diagnostics

Set `EDEN_FRAME_TRACE=1` before launching the emulator. This diagnostic mode records a sampled graphics frame every 300 emulated frames, under the active profile's `dump/draw-trace` directory. It supersedes `EDEN_TEXTURE_DIAGNOSTICS` while enabled. It does not change rendering policy or require gameplay automation.

To request a burst while the emulator is running, create an empty `capture.request` file in that directory. The rendering thread checks for it every 60 frames and attempts to capture the next six frames. Request a burst before changing an outfit and another after the defect appears. Copy the completed `.trace` files out of the directory promptly to preserve the comparison.

The 32 rotating files hold approximately 160 seconds at 60 FPS under periodic sampling; requested bursts shorten that history. Each frame has a 16 MiB record budget plus a small header/footer. At most two writes are queued or active. A dedicated worker writes temporary files and renames them into place; the rendering thread does not wait for disk. A busy writer drops a frame with an explicit log message. Truncated or interrupted frames carry `complete=false`; they must not be treated as complete evidence.

Records preserve order and distinguish submitted draws from rejected or compilation-pending draws. They include direct/indirect call metadata, guest vertex/index ranges and instance counts, pipeline hash, native shader addresses, constant-buffer addresses/sizes, texture view ranges, image flags, backing/decoding metadata and alias counts. Indirect argument contents are not read back. `submitted` means the command was recorded, not that its pixels passed GPU tests.

Image metadata is interned within each frame: `image_resource` defines an immutable resource snapshot and `image_ref` binds it to a draw/slot/TIC. Changed metadata creates a new resource definition. Every draw binding is retained.

`registers` records encode the complete Maxwell register array at the first draw and changed words thereafter. Indices and values are hexadecimal; indices are 32-bit word offsets in `Maxwell3D::Regs::reg_array`, not byte offsets. Deltas reset every captured frame. These records preserve render-target, viewport, scissor, alpha, blending, culling and depth state without duplicating unchanged values.

This is metadata tracing, not a full GPU replay: it does not capture pixel results, buffer/texture contents, compute resource bindings or every DMA/copy operation. It performs no additional GPU readback. Constant buffers describe binding identities, not material values. Failed draws have no resolved image records, to avoid confusing stale bindings with resources actually used. The trace can narrow down a missing draw or resource transition but cannot by itself prove why a submitted shader discards pixels.

Inspect a capture with `python3 tools/macos/summarize_frame_trace.py /absolute/path/frame-00.trace`. Add `--details` for per-draw state and image metadata. Compare captures from the same location before/after the style transition, using shader addresses plus geometry and resources, rather than assuming draw ordinals remain stable.

Logging and register comparison have a performance cost on captured frames. This mode is disabled unless explicitly enabled and should not be used for performance benchmarks. Native shader capture can be enabled separately through `EDEN_NX2_SHADER_DUMP` to retain source programs for offline analysis.
