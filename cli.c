#include <stdio.h>
#include <libpng16/png.h>
#include "fft.h"



typedef struct {
    uint16_t real;
} Pixel;

typedef struct {
    enum {
        RAW,
        ASCII,
        PNG
    } type;
    FILE* file;
} DataStream;
DataStream inputStream = {ASCII}, outputStream = {ASCII};

VkInstance instance;
VulkanFFTContext context = {};
VkFence fence;
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

void readDataStream(DataStream* dataStream, VulkanFFTPlan* vulkanFFT) {
    VulkanFFTTransfer vulkanFFTTransfer;
    vulkanFFTTransfer.context = &context;
    vulkanFFTTransfer.size = vulkanFFTPlan.bufferSize;
    vulkanFFTTransfer.deviceBuffer = vulkanFFTPlan.buffer[0];
    complex float* data = createVulkanFFTUpload(&vulkanFFTTransfer);
    switch(dataStream->type) {
        case RAW:
            assert(fread(data, 1, vulkanFFTPlan.bufferSize, dataStream->file) == vulkanFFTPlan.bufferSize);
            break;
        case ASCII:
            for(uint32_t i = 0; i < vulkanFFTPlan.axes[0].sampleCount * vulkanFFTPlan.axes[1].sampleCount * vulkanFFTPlan.axes[2].sampleCount; ++i) {
                float real, imag;
                fscanf(dataStream->file, "%f %f", &real, &imag);
                data[i] = real + imag * I;
            }
        break;
        case PNG: {
#ifdef PNG_LIBPNG_VER_STRING
            png_byte pngsig[8];
            assert(fread(pngsig, 1, sizeof(pngsig), dataStream->file) == sizeof(pngsig));
            assert(png_sig_cmp(pngsig, 0, sizeof(pngsig)) == 0);
            png_structp pngPtr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
            assert(pngPtr);
            png_infop infoPtr = png_create_info_struct(pngPtr);
            assert(infoPtr);
            png_byte** rowPtrs = malloc(sizeof(png_bytep) * vulkanFFTPlan.axes[1].sampleCount);
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
            assert(bitdepth == 16 && colorType == PNG_COLOR_TYPE_GRAY);
            for(uint32_t y = 0; y < vulkanFFTPlan.axes[1].sampleCount; ++y)
                rowPtrs[y] = (png_byte*)&data[vulkanFFTPlan.axes[0].sampleCount * y];
            png_set_swap(pngPtr);
            png_read_image(pngPtr, rowPtrs);
            for(uint32_t y = 0; y < vulkanFFTPlan.axes[1].sampleCount; ++y) {
                uint32_t yOffset = vulkanFFTPlan.axes[0].sampleCount * y;
                Pixel* row = (Pixel*)&data[yOffset];
                for(int32_t x = vulkanFFTPlan.axes[0].sampleCount-1; x >= 0; --x)
                    data[x + yOffset] = (row[x].real / 32767.0) - 1.0;
            }
            png_read_end(pngPtr, NULL);
            free(rowPtrs);
#endif
        } break;
    }
    freeVulkanFFTTransfer(&vulkanFFTTransfer);
}

void writeDataStream(DataStream* dataStream, VulkanFFTPlan* vulkanFFT) {
    VulkanFFTTransfer vulkanFFTTransfer;
    vulkanFFTTransfer.context = &context;
    vulkanFFTTransfer.size = vulkanFFTPlan.bufferSize;
    vulkanFFTTransfer.deviceBuffer = vulkanFFTPlan.buffer[vulkanFFTPlan.resultInSwapBuffer];
    complex float* data = createVulkanFFTDownload(&vulkanFFTTransfer);
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
                        fprintf(dataStream->file, "%f %f ", crealf(data[x + yzOffset]), cimagf(data[x + yzOffset]));
                    fprintf(dataStream->file, "\n");
                }
            }
            break;
        case PNG: {
#ifdef PNG_LIBPNG_VER_STRING
            png_structp pngPtr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
            assert(pngPtr);
            png_infop infoPtr = png_create_info_struct(pngPtr);
            assert(infoPtr);
            png_byte** rowPtrs = malloc(sizeof(png_bytep) * vulkanFFTPlan.axes[1].sampleCount);
            if(setjmp(png_jmpbuf(pngPtr))) {
                png_destroy_read_struct(&pngPtr, &infoPtr, (png_infopp)0);
                free(rowPtrs);
                abortWithError("Could not generate PNG output");
            }
            png_init_io(pngPtr, dataStream->file);
            png_set_IHDR(pngPtr, infoPtr, vulkanFFTPlan.axes[0].sampleCount, vulkanFFTPlan.axes[1].sampleCount, 16, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
            png_write_info(pngPtr, infoPtr);
            for(uint32_t y = 0; y < vulkanFFTPlan.axes[1].sampleCount; ++y) {
                uint32_t yOffset = vulkanFFTPlan.axes[0].sampleCount * y;
                Pixel* row = (Pixel*)&data[yOffset];
                rowPtrs[y] = (png_byte*)row;
                for(uint32_t x = 0; x < vulkanFFTPlan.axes[0].sampleCount; ++x)
                    row[x].real = (crealf(data[x + yOffset]) + 1.0) * 32767.0;
            }
            png_set_swap(pngPtr);
            png_write_image(pngPtr, rowPtrs);
            png_write_end(pngPtr, NULL);
            free(rowPtrs);
#endif
        } break;
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
    bool listDevices = false;
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
            else if(strcmp(argv[i], "png") == 0)
                dataStream->type = PNG;
        } else if(strcmp(argv[i], "--device") == 0) {
            assert(++i < argc);
            sscanf(argv[i], "%d", &deviceIndex);
        } else if(strcmp(argv[i], "--list-devices") == 0)
            listDevices = true;
        else
            fprintf(stderr, "Unrecognized option %s\n", argv[i]);
    }

    {
        uint32_t instanceLayerCount;
        assert(vkEnumerateInstanceLayerProperties(&instanceLayerCount, NULL) == VK_SUCCESS);
        if(instanceLayerCount == 0)
            abortWithError("No layers found: Is VK_LAYER_PATH set correctly?");
        VkLayerProperties instanceLayers[instanceLayerCount];
        assert(vkEnumerateInstanceLayerProperties(&instanceLayerCount, instanceLayers) == VK_SUCCESS);
        VkInstanceCreateInfo instanceCreateInfo = {};
        const char* layers[] = {
            #ifndef NDEBUG
            "VK_LAYER_LUNARG_standard_validation"
            #endif
        };
        instanceCreateInfo.enabledLayerCount = COUNT_OF(layers);
        instanceCreateInfo.ppEnabledLayerNames = layers;
        const char* extensions[] = {
            #ifndef NDEBUG
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME
            #endif
        };
        instanceCreateInfo.enabledExtensionCount = COUNT_OF(extensions);
        instanceCreateInfo.ppEnabledExtensionNames = extensions;
        VkResult result = vkCreateInstance(&instanceCreateInfo, context.allocator, &instance);
        if(result == VK_ERROR_INCOMPATIBLE_DRIVER)
            abortWithError("No driver found, could not create instance: Is VK_ICD_FILENAMES set correctly?");
        assert(result == VK_SUCCESS);
    }

    #ifndef NDEBUG
    {
        VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo = {};
        debugUtilsMessengerCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugUtilsMessengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
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
        vkGetPhysicalDeviceMemoryProperties(context.physicalDevice, &context.physicalDeviceMemoryProperties);

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

        VkFenceCreateInfo fenceCreateInfo = {};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.flags = 0;
        assert(vkCreateFence(context.device, &fenceCreateInfo, context.allocator, &fence) == VK_SUCCESS);
    }

    context.shaderModule = loadShaderModule(&context, "fft.spv");

    if(!listDevices) {
        createVulkanFFT(&vulkanFFTPlan);
        readDataStream(&inputStream, &vulkanFFTPlan);

        VkCommandBuffer commandBuffer = createCommandBuffer(&context, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        recordVulkanFFT(&vulkanFFTPlan, commandBuffer);
        vkEndCommandBuffer(commandBuffer);
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        assert(vkQueueSubmit(context.queue, 1, &submitInfo, fence) == VK_SUCCESS);
        assert(vkWaitForFences(context.device, 1, &fence, VK_TRUE, 100000000000) == VK_SUCCESS);

        writeDataStream(&outputStream, &vulkanFFTPlan);
        destroyVulkanFFT(&vulkanFFTPlan);
    }

    vkDestroyShaderModule(context.device, context.shaderModule, context.allocator);
    vkDestroyFence(context.device, fence, context.allocator);
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
