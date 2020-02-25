| All | Linux | macOS | Windows |
| --- | ----- | ----- | ------- |
| [![Build Status](https://api.travis-ci.org/Lichtso/VulkanFFT.svg)](https://travis-ci.org/Lichtso/VulkanFFT) | [![Build Status Linux](https://travis-matrix-badges.herokuapp.com/repos/Lichtso/VulkanFFT/branches/master/1)](https://travis-ci.org/Lichtso/VulkanFFT) | [![Build Status macOS](https://travis-matrix-badges.herokuapp.com/repos/Lichtso/VulkanFFT/branches/master/2)](https://travis-ci.org/Lichtso/VulkanFFT) | [![Build Status Windows](https://travis-matrix-badges.herokuapp.com/repos/Lichtso/VulkanFFT/branches/master/3)](https://travis-ci.org/Lichtso/VulkanFFT) |

# Vulkan Fast Fourier Transform
This library can calculate a multidimensional [Discrete Fourier Transform](https://en.wikipedia.org/wiki/Discrete_Fourier_transform) on the GPU using the [Vulkan API](https://www.khronos.org/vulkan/).
However, in most cases you probably want a different library,
because Vulkan does not change much about the GPU computations,
but is a lot more complex than other APIs.
Alternatives are based on [OpenGL](https://github.com/Themaister/GLFFT), [OpenCL](https://gpuopen.com/compute-product/clfft/) or [CUDA](https://developer.nvidia.com/cufft) for example.
Some reasons to use this library anyway are:
- You already have a Vulkan application and just need a FFT implementation
- You want to reduce CPU load, because Vulkan is meant do exactly that
- You are on a platform where other options are worse or just not supported (e.g. OpenGL on MacOS)
- You are just here for the CLI and don't care about Vulkan

## Command Line Interface
Note that many small invocations are very inefficient, because the startup costs of Vulkan are very high.
So the CLI is only useful for transforming big data sets and testing.
Also, only the library is supported on windows, not the CLI.

### Options
- `-x width` Samples in x direction
- `-y height` Samples in y direction
- `-z depth` Samples in z direction
- `--inverse` Calculate the IDFT
- `--input raw / ascii / png / exr` Input encoding
- `--output raw / ascii / png / exr` Output encoding
- `--device index` Vulkan device to use
- `--list-devices` List Vulkan devices
- `--measure-time` Measure time spent in setup, upload, computation, download and teardown

### Example Invocations
```bash
vulkanfft -x 16 -y 16 --input ascii --output png --inverse < test.txt > test.png
vulkanfft -x 16 -y 16 --input png --output ascii < test.png
```

## Dependencies
- cmake 3.11
- Vulkan Runtime 1.0
- Vulkan SDK 1.2.131.2 (to compile GLSL to SPIR-V)
- xxd (to inline SPIR-V in C)
- libpng 1.6 (optional, only needed for CLI)
- libopenexr 2.3 (optional, only needed for CLI)

## Current Features & Limitations
- Dimensions
    - Only 1D, 2D, 3D
    - No higher dimensions
- Direction & Scale
    - Forward / backward (inverse)
    - No normalized / unnormalized switch independent of direction
- Sample Count / Size
    - Only POT (power of two)
    - No prime factorization
- Memory Layout
    - Only buffers (row-major order)
    - No samplers / images / textures (2D tiling)
    - Only interleaved complex float
    - No separation of real and imaginary parts
- Bit-Depth & Data Types
    - Only 32 bit complex floats
    - No real only mode
    - No 8, 16, 64, 128 bit floats or integers
- Parallelization / SIMD
    - Only radix 2, 4, 8
    - No higher radix
- Memory Requirements
    - 2*n because of swap buffers for Stockham auto-sort algorithm
    - No reordering and in-place operation
- Memorization & Profiling
    - Only cold planning
    - No memorization or warm planning / wisdom profiling
- Related Extras
    - No convolution
