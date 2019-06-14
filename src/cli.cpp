#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <complex>
#include <chrono>
#ifdef HAS_PNG
#include <libpng16/png.h>
#endif
#ifdef HAS_EXR
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfOutputFile.h>
#endif
extern "C" {
#include "VulkanFFT.h"
}
#define COUNT_OF(array) (sizeof(array) / sizeof(array[0]))



typedef uint8_t Pixel;

enum IOType {
    RAW,
    ASCII,
#ifdef HAS_PNG
    PNG,
#endif
#ifdef HAS_EXR
    EXR,
#endif
};
typedef struct {
    IOType type;
    FILE* file;
} DataStream;
DataStream inputStream = {ASCII}, outputStream = {ASCII};

VkInstance instance;
VulkanFFTContext context = {};
VulkanFFTPlan vulkanFFTPlan = {&context};

#ifndef NDEBUG
VkDebugUtilsMessengerEXT debugMessenger;
static VKAPI_ATTR VkBool32 VKAPI_CALL vkDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                      VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                      void* pUserData) {
    fprintf(stderr, "VULKAN: %s\n", pCallbackData->pMessage);
    return VK_FALSE;
}
#endif

void abortWithError(const char* message) {
    fprintf(stderr, "%s\n", message);
    exit(1);
}

#ifdef HAS_EXR
Imf::FrameBuffer frameBufferForEXR(float* image, uint32_t width) {
    Imf::FrameBuffer frameBuffer;
    uint32_t xStride = 2*sizeof(float);
    uint32_t yStride = width*2*sizeof(float);
    frameBuffer.insert("R", Imf::Slice(Imf::FLOAT, (char*)&image[0], xStride, yStride));
    frameBuffer.insert("G", Imf::Slice(Imf::FLOAT, (char*)&image[1], xStride, yStride));
    return frameBuffer;
}
#endif

void readDataStream(DataStream* dataStream, VulkanFFTPlan* vulkanFFT) {
    VulkanFFTTransfer vulkanFFTTransfer;
    vulkanFFTTransfer.context = &context;
    vulkanFFTTransfer.size = vulkanFFTPlan.bufferSize;
    vulkanFFTTransfer.deviceBuffer = vulkanFFTPlan.buffer[0];
    auto data = reinterpret_cast<std::complex<float>*>(createVulkanFFTUpload(&vulkanFFTTransfer));
    switch(dataStream->type) {
        case RAW:
            assert(fread(data, 1, vulkanFFTPlan.bufferSize, dataStream->file) == vulkanFFTPlan.bufferSize);
            break;
        case ASCII:
            for(uint32_t i = 0; i < vulkanFFTPlan.axes[0].sampleCount * vulkanFFTPlan.axes[1].sampleCount * vulkanFFTPlan.axes[2].sampleCount; ++i) {
                float real, imag;
                fscanf(dataStream->file, "%f %f", &real, &imag);
                data[i] = std::complex<float>(real, imag);
            }
        break;
#ifdef HAS_PNG
        case PNG: {
            png_byte pngsig[8];
            assert(fread(pngsig, 1, sizeof(pngsig), dataStream->file) == sizeof(pngsig));
            assert(png_sig_cmp(pngsig, 0, sizeof(pngsig)) == 0);
            png_structp pngPtr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
            assert(pngPtr);
            png_infop infoPtr = png_create_info_struct(pngPtr);
            assert(infoPtr);
            auto rowPtrs = reinterpret_cast<png_byte**>(malloc(sizeof(png_bytep) * vulkanFFTPlan.axes[1].sampleCount));
            if(setjmp(png_jmpbuf(pngPtr))) {
                png_destroy_read_struct(&pngPtr, &infoPtr, (png_infopp)0);
                free(rowPtrs);
                abortWithError("Could not parse PNG input");
            }
            png_init_io(pngPtr, dataStream->file);
            png_set_sig_bytes(pngPtr, sizeof(pngsig));
            png_read_info(pngPtr, infoPtr);
            png_set_expand(pngPtr);
            png_read_update_info(pngPtr, infoPtr);
            png_uint_32 width, height;
            int bitdepth, colorType;
            png_get_IHDR(pngPtr, infoPtr, &width, &height, &bitdepth, &colorType, NULL, NULL, NULL);
            assert(vulkanFFTPlan.axes[0].sampleCount == width && vulkanFFTPlan.axes[1].sampleCount == height && vulkanFFTPlan.axes[2].sampleCount == 1);
            assert(bitdepth == 8 && colorType == PNG_COLOR_TYPE_GRAY);
            for(uint32_t y = 0; y < vulkanFFTPlan.axes[1].sampleCount; ++y)
                rowPtrs[y] = (png_byte*)&data[vulkanFFTPlan.axes[0].sampleCount * y];
            png_set_swap(pngPtr);
            png_read_image(pngPtr, rowPtrs);
            for(uint32_t y = 0; y < vulkanFFTPlan.axes[1].sampleCount; ++y) {
                uint32_t yOffset = vulkanFFTPlan.axes[0].sampleCount * y;
                Pixel* row = (Pixel*)&data[yOffset];
                for(int32_t x = vulkanFFTPlan.axes[0].sampleCount-1; x >= 0; --x)
                    data[x + yOffset] = (float)row[x] / 256.0;
            }
            png_read_end(pngPtr, NULL);
            free(rowPtrs);
        } break;
#endif
#ifdef HAS_EXR
        case EXR: {
            Imf::InputFile inputFile("/dev/stdin");
            assert(inputFile.header().channels().findChannel("R") && inputFile.header().channels().findChannel("G"));
            Imath::Box2i dataWindow = inputFile.header().dataWindow();
            uint32_t width = dataWindow.max.x-dataWindow.min.x+1, height = dataWindow.max.y-dataWindow.min.y+1;
            assert(vulkanFFTPlan.axes[0].sampleCount == width && vulkanFFTPlan.axes[1].sampleCount == height && vulkanFFTPlan.axes[2].sampleCount == 1);
            inputFile.setFrameBuffer(frameBufferForEXR(reinterpret_cast<float*>(data), width));
            inputFile.readPixels(dataWindow.min.y, dataWindow.max.y);
        } break;
#endif
    }
    freeVulkanFFTTransfer(&vulkanFFTTransfer);
}

void writeDataStream(DataStream* dataStream, VulkanFFTPlan* vulkanFFT) {
    VulkanFFTTransfer vulkanFFTTransfer;
    vulkanFFTTransfer.context = &context;
    vulkanFFTTransfer.size = vulkanFFTPlan.bufferSize;
    vulkanFFTTransfer.deviceBuffer = vulkanFFTPlan.buffer[vulkanFFTPlan.resultInSwapBuffer];
    auto data = reinterpret_cast<std::complex<float>*>(createVulkanFFTDownload(&vulkanFFTTransfer));
    switch(dataStream->type) {
        case RAW:
            assert(fwrite(data, 1, vulkanFFTPlan.bufferSize, dataStream->file) == vulkanFFTPlan.bufferSize);
            break;
        case ASCII:
            for(uint32_t z = 0; z < vulkanFFTPlan.axes[2].sampleCount; ++z) {
                uint32_t zOffset = vulkanFFTPlan.axes[0].sampleCount * vulkanFFTPlan.axes[1].sampleCount * z;
                if(z > 0)
                    fprintf(dataStream->file, "\n");
                for(uint32_t y = 0; y < vulkanFFTPlan.axes[1].sampleCount; ++y) {
                    uint32_t yzOffset = zOffset + vulkanFFTPlan.axes[0].sampleCount * y;
                    for(uint32_t x = 0; x < vulkanFFTPlan.axes[0].sampleCount; ++x)
                        fprintf(dataStream->file, "%.24f %.24f ", std::real(data[x + yzOffset]), std::imag(data[x + yzOffset]));
                    fprintf(dataStream->file, "\n");
                }
            }
            break;
#ifdef HAS_PNG
        case PNG: {
            png_structp pngPtr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
            assert(pngPtr);
            png_infop infoPtr = png_create_info_struct(pngPtr);
            assert(infoPtr);
            auto rowPtrs = reinterpret_cast<png_byte**>(malloc(sizeof(png_bytep) * vulkanFFTPlan.axes[1].sampleCount));
            if(setjmp(png_jmpbuf(pngPtr))) {
                png_destroy_read_struct(&pngPtr, &infoPtr, (png_infopp)0);
                free(rowPtrs);
                abortWithError("Could not generate PNG output");
            }
            png_init_io(pngPtr, dataStream->file);
            png_set_IHDR(pngPtr, infoPtr, vulkanFFTPlan.axes[0].sampleCount, vulkanFFTPlan.axes[1].sampleCount, 8, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
            png_write_info(pngPtr, infoPtr);
            for(uint32_t y = 0; y < vulkanFFTPlan.axes[1].sampleCount; ++y) {
                uint32_t yOffset = vulkanFFTPlan.axes[0].sampleCount * y;
                Pixel* row = (Pixel*)&data[yOffset];
                rowPtrs[y] = (png_byte*)row;
                for(uint32_t x = 0; x < vulkanFFTPlan.axes[0].sampleCount; ++x)
                    row[x] = std::real(data[x + yOffset]) * 256.0;
            }
            png_set_swap(pngPtr);
            png_write_image(pngPtr, rowPtrs);
            png_write_end(pngPtr, NULL);
            free(rowPtrs);
        } break;
#endif
#ifdef HAS_EXR
        case EXR: {
            Imf::Header header(vulkanFFTPlan.axes[0].sampleCount, vulkanFFTPlan.axes[1].sampleCount, 1, Imath::V2f(0, 0), vulkanFFTPlan.axes[0].sampleCount, Imf::INCREASING_Y, Imf::ZIP_COMPRESSION);
            header.channels().insert("R", Imf::Channel(Imf::FLOAT));
            header.channels().insert("G", Imf::Channel(Imf::FLOAT));
            Imf::OutputFile outputFile("/dev/stdout", header);
            outputFile.setFrameBuffer(frameBufferForEXR(reinterpret_cast<float*>(data), vulkanFFTPlan.axes[0].sampleCount));
            outputFile.writePixels(vulkanFFTPlan.axes[1].sampleCount);
        } break;
#endif
    }
    freeVulkanFFTTransfer(&vulkanFFTTransfer);
}

int main(int argc, const char** argv) {
    uint32_t deviceIndex = 0;
    vulkanFFTPlan.axes[0].sampleCount = 1;
    vulkanFFTPlan.axes[1].sampleCount = 1;
    vulkanFFTPlan.axes[2].sampleCount = 1;
    inputStream.file = stdin;
    outputStream.file = stdout;
    bool listDevices = false,
         measureTime = false;
    for(uint32_t i = 1; i < argc; ++i) {
        if(strcmp(argv[i], "-x") == 0) {
            assert(++i < argc);
            sscanf(argv[i], "%d", &vulkanFFTPlan.axes[0].sampleCount);
        } else if(strcmp(argv[i], "-y") == 0) {
            assert(++i < argc);
            sscanf(argv[i], "%d", &vulkanFFTPlan.axes[1].sampleCount);
        } else if(strcmp(argv[i], "-z") == 0) {
            assert(++i < argc);
            sscanf(argv[i], "%d", &vulkanFFTPlan.axes[2].sampleCount);
        } else if(strcmp(argv[i], "--inverse") == 0)
            vulkanFFTPlan.inverse = true;
        else if(strcmp(argv[i], "--input") == 0 || strcmp(argv[i], "--output") == 0) {
            DataStream* dataStream = (strcmp(argv[i], "--input") == 0) ? &inputStream : &outputStream;
            assert(++i < argc);
            if(strcmp(argv[i], "raw") == 0)
                dataStream->type = RAW;
            else if(strcmp(argv[i], "ascii") == 0)
                dataStream->type = ASCII;
#ifdef HAS_PNG
            else if(strcmp(argv[i], "png") == 0)
                dataStream->type = PNG;
#endif
#ifdef HAS_EXR
            else if(strcmp(argv[i], "exr") == 0)
                dataStream->type = EXR;
#endif
        } else if(strcmp(argv[i], "--device") == 0) {
            assert(++i < argc);
            sscanf(argv[i], "%d", &deviceIndex);
        } else if(strcmp(argv[i], "--list-devices") == 0)
            listDevices = true;
         else if(strcmp(argv[i], "--measure-time") == 0)
            measureTime = true;
        else
            fprintf(stderr, "Unrecognized option %s\n", argv[i]);
    }

    {
        const char* requiredLayers[] = {
            #ifndef NDEBUG
            "VK_LAYER_LUNARG_standard_validation"
            #endif
        };
        const char* requiredExtensions[] = {
            #ifndef NDEBUG
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME
            #endif
        };
        uint32_t instanceLayerCount;
        assert(vkEnumerateInstanceLayerProperties(&instanceLayerCount, NULL) == VK_SUCCESS);
        if(instanceLayerCount == 0)
            abortWithError("No layers found: Is VK_LAYER_PATH set correctly?");
        VkLayerProperties instanceLayers[instanceLayerCount];
        assert(vkEnumerateInstanceLayerProperties(&instanceLayerCount, instanceLayers) == VK_SUCCESS);
        for(uint32_t i = 0; i < COUNT_OF(requiredLayers); ++i) {
            bool found = false;
            for(uint32_t j = 0; j < instanceLayerCount; ++j)
                if(strcmp(requiredLayers[i], instanceLayers[j].layerName) == 0) {
                    found = true;
                    break;
                }
            if(!found) {
                fprintf(stderr, "Required layer %s was not found\n", requiredLayers[i]);
                exit(1);
            }
        }
        uint32_t extensionCount;
        vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
        VkExtensionProperties extensions[extensionCount];
        vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, extensions);
        for(uint32_t i = 0; i < COUNT_OF(requiredExtensions); ++i) {
            bool found = false;
            for(uint32_t j = 0; j < extensionCount; ++j)
                if(strcmp(requiredExtensions[i], extensions[j].extensionName) == 0) {
                    found = true;
                    break;
                }
            if(!found) {
                fprintf(stderr, "Required extension %s was not found\n", requiredExtensions[i]);
                exit(1);
            }
        }
        VkInstanceCreateInfo instanceCreateInfo = {};
        instanceCreateInfo.enabledLayerCount = COUNT_OF(requiredLayers);
        instanceCreateInfo.ppEnabledLayerNames = requiredLayers;
        instanceCreateInfo.enabledExtensionCount = COUNT_OF(requiredExtensions);
        instanceCreateInfo.ppEnabledExtensionNames = requiredExtensions;
        VkResult result = vkCreateInstance(&instanceCreateInfo, context.allocator, &instance);
        if(result == VK_ERROR_INCOMPATIBLE_DRIVER)
            abortWithError("No driver found, could not create instance: Is VK_ICD_FILENAMES set correctly?");
        assert(result == VK_SUCCESS);
    }

    #ifndef NDEBUG
    {
        VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo = {};
        debugUtilsMessengerCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugUtilsMessengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugUtilsMessengerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugUtilsMessengerCreateInfo.pfnUserCallback = vkDebugCallback;
        debugUtilsMessengerCreateInfo.pUserData = NULL;
        PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        assert(func);
        assert(func(instance, &debugUtilsMessengerCreateInfo, context.allocator, &debugMessenger) == VK_SUCCESS);
    }
    #endif

    {
        uint32_t physicalDeviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, NULL);
        if(physicalDeviceCount == 0)
            abortWithError("No devices found");
        if(deviceIndex >= physicalDeviceCount)
            abortWithError("Device index too high");
        VkPhysicalDevice physicalDevices[physicalDeviceCount];
        vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices);
        for(int i = 0; i < physicalDeviceCount; ++i) {
            VkPhysicalDeviceProperties deviceProperties;
            vkGetPhysicalDeviceProperties(physicalDevices[i], &deviceProperties);
            if(listDevices)
                fprintf(stderr, "Device %d: %s\n", i, deviceProperties.deviceName);
            VkPhysicalDeviceFeatures deviceFeatures;
            vkGetPhysicalDeviceFeatures(physicalDevices[i], &deviceFeatures);
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[i], &queueFamilyCount, NULL);
            VkQueueFamilyProperties queueFamilies[queueFamilyCount];
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[i], &queueFamilyCount, queueFamilies);
        }
        context.physicalDevice = physicalDevices[deviceIndex];

        VkDeviceQueueCreateInfo deviceQueueCreateInfo = {};
        deviceQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        deviceQueueCreateInfo.queueFamilyIndex = 0;
        deviceQueueCreateInfo.queueCount = 1;
        float queuePriority = 1.0;
        deviceQueueCreateInfo.pQueuePriorities = &queuePriority;

        VkPhysicalDeviceFeatures deviceFeatures = {};
        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pQueueCreateInfos = &deviceQueueCreateInfo;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
        deviceCreateInfo.enabledLayerCount = 0;
        assert(vkCreateDevice(context.physicalDevice, &deviceCreateInfo, context.allocator, &context.device) == VK_SUCCESS);
        vkGetDeviceQueue(context.device, deviceQueueCreateInfo.queueFamilyIndex, 0, &context.queue);

        VkCommandPoolCreateInfo commandPoolCreateInfo = {};
        commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolCreateInfo.queueFamilyIndex = 0;
        assert(vkCreateCommandPool(context.device, &commandPoolCreateInfo, context.allocator, &context.commandPool) == VK_SUCCESS);
    }

    if(!listDevices) {
        auto timeA = std::chrono::steady_clock::now();
        initVulkanFFTContext(&context);
        createVulkanFFT(&vulkanFFTPlan);
        VkCommandBuffer commandBuffer = createCommandBuffer(&context, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        recordVulkanFFT(&vulkanFFTPlan, commandBuffer);
        vkEndCommandBuffer(commandBuffer);
        auto timeB = std::chrono::steady_clock::now();
        readDataStream(&inputStream, &vulkanFFTPlan);
        auto timeC = std::chrono::steady_clock::now();
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        assert(vkQueueSubmit(context.queue, 1, &submitInfo, context.fence) == VK_SUCCESS);
        assert(vkWaitForFences(context.device, 1, &context.fence, VK_TRUE, 100000000000) == VK_SUCCESS);
        assert(vkResetFences(context.device, 1, &context.fence) == VK_SUCCESS);
        auto timeD = std::chrono::steady_clock::now();
        writeDataStream(&outputStream, &vulkanFFTPlan);
        auto timeE = std::chrono::steady_clock::now();
        destroyVulkanFFT(&vulkanFFTPlan);
        freeVulkanFFTContext(&context);
        auto timeF = std::chrono::steady_clock::now();
        if(measureTime) {
            fprintf(stderr, "Setup: %.3f ms\n", std::chrono::duration_cast<std::chrono::microseconds>(timeB-timeA).count()*0.001);
            fprintf(stderr, "Upload: %.3f ms\n", std::chrono::duration_cast<std::chrono::microseconds>(timeC-timeB).count()*0.001);
            fprintf(stderr, "Computation: %.3f ms\n", std::chrono::duration_cast<std::chrono::microseconds>(timeD-timeC).count()*0.001);
            fprintf(stderr, "Download: %.3f ms\n", std::chrono::duration_cast<std::chrono::microseconds>(timeE-timeD).count()*0.001);
            fprintf(stderr, "Teardown: %.3f ms\n", std::chrono::duration_cast<std::chrono::microseconds>(timeF-timeE).count()*0.001);
        }
    }

    vkDestroyCommandPool(context.device, context.commandPool, context.allocator);
    vkDestroyDevice(context.device, NULL);
    #ifndef NDEBUG
    {
        PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        assert(func);
        func(instance, debugMessenger, context.allocator);
    }
    #endif
    vkDestroyInstance(instance, NULL);
    return 0;
}
