# Vulkan Fast Fourier Transform
This libary can calculate a multidimensional [Discrete Fourier Transform](https://en.wikipedia.org/wiki/Discrete_Fourier_transform) in O(n * log(n)) on the GPU using the [Vulkan API](https://www.khronos.org/vulkan/).


## Command Line Interface
Don't forget to set the environment variables to something like this:
```bash
VULKAN_SDK=path/to/vulkan/installation
export VK_ICD_FILENAMES=$VULKAN_SDK/etc/vulkan/icd.d/driver_icd.json
export VK_LAYER_PATH=$VULKAN_SDK/etc/vulkan/explicit_layer.d
```

### Options
- `-x width` Samples in x direction
- `-y height` Samples in y direction
- `-z depth` Samples in z direction
- `--inverse` Calculate the IDFT
- `--input raw / ascii / png` Input encoding
- `--output raw / ascii / png` Output encoding
- `--device index` Vulkan device to use
- `--list-devices` List vulkan devices

### Example Invocations
```bash
./cli -x 16 -y 16 --input ascii --output png --inverse < test.txt > test.png
./cli -x 16 -y 16 --input png --output ascii < test.png
```


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
    - Only radix 2
    - No higher radix
- Memory Requirements
    - 2*n because of swap buffers for Stockham auto-sort algorithm
    - No reordering and in-place operation
- Memorization & Profiling
    - Only cold planning
    - No memorization or warm planning / wisdom profiling
