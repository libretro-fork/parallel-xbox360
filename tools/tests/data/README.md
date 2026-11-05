`resolve_downscale_reference.spv` is the core's original downscale module from
commit fa38c37c0681ef745c628c9fc86035f08b3792a6. The Vulkan differential test
compares it with the direct-destination module on the GPU. This is test data,
not a runtime shader dependency.

`tools/tests/run_gpu_downscale_tests.cmd` runs the MSVC C89 planner and both GPU
fixtures. The shell runner checks the planner, with optional `GPU_TESTS=1` for
Vulkan and, on MinGW, D3D12 WARP. GPU fixtures compare every pixel width, scale
axis from 1 through 8, half-pixel mode, and destination alignment, including
split dispatches and sentinel bytes surrounding the destination.

The checked-in `resolve_downscale_cs.spvasm` is the editable raw SPIR-V source.
Regenerate its adjacent binary using development-only SPIRV-Tools:

```
spirv-as --target-env spv1.5 resolve_downscale_cs.spvasm -o resolve_downscale_cs.spv
spirv-val --target-env vulkan1.2 resolve_downscale_cs.spv
```

No shader compiler, assembler or validator is required to build the core. The
D3D12 module is unchanged: raw UAV `FirstElement` selects the exact destination,
so its unused seventh root constant remains zero.
