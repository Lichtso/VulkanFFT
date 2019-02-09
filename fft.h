#include <math.h>
#include <complex.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vulkan/vulkan.h>
#define COUNT_OF(array) (sizeof(array) / sizeof(array[0]))



typedef struct {
    VkAllocationCallbacks* allocator;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties;
    VkDevice device;
    VkQueue queue;
    VkCommandPool commandPool;
    VkShaderModule shaderModule;
} VulkanFFTContext;

VkShaderModule loadShaderModule(VulkanFFTContext* context, const char* filename) {
    FILE* file = fopen(filename, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long codeSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* code = malloc(codeSize);
    assert(code);
    fread(code, 1, codeSize, file);
    fclose(file);
    VkShaderModuleCreateInfo shaderModuleCreateInfo = {};
    shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCreateInfo.codeSize = codeSize;
    shaderModuleCreateInfo.pCode = (const uint32_t*)code;
    VkShaderModule shaderModule;
    assert(vkCreateShaderModule(context->device, &shaderModuleCreateInfo, context->allocator, &shaderModule) == VK_SUCCESS);
    free(code);
    return shaderModule;
}

uint32_t findMemoryType(VulkanFFTContext* context, uint32_t typeFilter, VkMemoryPropertyFlags propertyFlags) {
    for(uint32_t i = 0; i < context->physicalDeviceMemoryProperties.memoryTypeCount; ++i)
        if((typeFilter & (1 << i)) && (context->physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & propertyFlags) == propertyFlags)
            return i;
    return 0;
}

void createBuffer(VulkanFFTContext* context, VkBuffer* buffer, VkDeviceMemory* deviceMemory, VkBufferUsageFlags usage, VkMemoryPropertyFlags propertyFlags, VkDeviceSize size) {
    uint32_t queueFamilyIndices[1] = {0};
    VkBufferCreateInfo bufferCreateInfo = {};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.queueFamilyIndexCount = COUNT_OF(queueFamilyIndices);
    bufferCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage = usage;
    assert(vkCreateBuffer(context->device, &bufferCreateInfo, context->allocator, buffer) == VK_SUCCESS);
    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(context->device, *buffer, &memoryRequirements);
    VkMemoryAllocateInfo memoryAllocateInfo = {};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.allocationSize = memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = findMemoryType(context, memoryRequirements.memoryTypeBits, propertyFlags);
    assert(vkAllocateMemory(context->device, &memoryAllocateInfo, context->allocator, deviceMemory) == VK_SUCCESS);
    vkBindBufferMemory(context->device, *buffer, *deviceMemory, 0);
}

VkCommandBuffer createCommandBuffer(VulkanFFTContext* context, VkCommandBufferUsageFlags usageFlags) {
    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.commandPool = context->commandPool;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer;
    assert(vkAllocateCommandBuffers(context->device, &commandBufferAllocateInfo, &commandBuffer) == VK_SUCCESS);
    VkCommandBufferBeginInfo commandBufferBeginInfo = {};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBeginInfo.flags = usageFlags;
    assert(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) == VK_SUCCESS);
    return commandBuffer;
}



typedef struct {
    VulkanFFTContext* context;
    VkDeviceSize size;
    VkBuffer hostBuffer, deviceBuffer;
    VkDeviceMemory deviceMemory;
} VulkanFFTTransfer;

void bufferTransfer(VulkanFFTContext* context, VkBuffer dstBuffer, VkBuffer srcBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = createCommandBuffer(context, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(context->queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(context->queue);
    vkFreeCommandBuffers(context->device, context->commandPool, 1, &commandBuffer);
}

void* createVulkanFFTUpload(VulkanFFTTransfer* vulkanFFTTransfer) {
    createBuffer(vulkanFFTTransfer->context, &vulkanFFTTransfer->hostBuffer, &vulkanFFTTransfer->deviceMemory, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vulkanFFTTransfer->size);
    void* map;
    vkMapMemory(vulkanFFTTransfer->context->device, vulkanFFTTransfer->deviceMemory, 0, vulkanFFTTransfer->size, 0, &map);
    return map;
}

void* createVulkanFFTDownload(VulkanFFTTransfer* vulkanFFTTransfer) {
    createBuffer(vulkanFFTTransfer->context, &vulkanFFTTransfer->hostBuffer, &vulkanFFTTransfer->deviceMemory, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vulkanFFTTransfer->size);
    bufferTransfer(vulkanFFTTransfer->context, vulkanFFTTransfer->hostBuffer, vulkanFFTTransfer->deviceBuffer, vulkanFFTTransfer->size);
    vulkanFFTTransfer->deviceBuffer = VK_NULL_HANDLE;
    void* map;
    vkMapMemory(vulkanFFTTransfer->context->device, vulkanFFTTransfer->deviceMemory, 0, vulkanFFTTransfer->size, 0, &map);
    return map;
}

void freeVulkanFFTTransfer(VulkanFFTTransfer* vulkanFFTTransfer) {
    if(vulkanFFTTransfer->deviceBuffer)
        bufferTransfer(vulkanFFTTransfer->context, vulkanFFTTransfer->deviceBuffer, vulkanFFTTransfer->hostBuffer, vulkanFFTTransfer->size);
    vkUnmapMemory(vulkanFFTTransfer->context->device, vulkanFFTTransfer->deviceMemory);
    vkDestroyBuffer(vulkanFFTTransfer->context->device, vulkanFFTTransfer->hostBuffer, vulkanFFTTransfer->context->allocator);
    vkFreeMemory(vulkanFFTTransfer->context->device, vulkanFFTTransfer->deviceMemory, vulkanFFTTransfer->context->allocator);
}

typedef union {
    struct {
        uint32_t strideX, strideY, strideZ;
        uint32_t radixStride, stageSize;
        float angleFactor;
        float normalizationFactor;
    };
    unsigned char padding[0x100];
} UBO;

typedef struct {
    uint32_t sampleCount;
    uint32_t stageCount;
    uint32_t* stageRadix;
    VkDeviceSize uboSize;
    VkBuffer ubo;
    VkDeviceMemory uboDeviceMemory;
    VkDescriptorPool descriptorPool;
    VkDescriptorSetLayout* descriptorSetLayouts;
    VkDescriptorSet* descriptorSets;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
} VulkanFFTAxis;

typedef struct {
    VulkanFFTContext* context;
    bool inverse, resultInSwapBuffer;
    VulkanFFTAxis axes[3];
    VkDeviceSize bufferSize;
    VkBuffer buffer[2];
    VkDeviceMemory deviceMemory[2];
} VulkanFFTPlan;

void planVulkanFFTAxis(VulkanFFTPlan* vulkanFFTPlan, uint32_t axis) {
    VulkanFFTAxis* vulkanFFTAxis = &vulkanFFTPlan->axes[axis];

    vulkanFFTAxis->stageCount = __builtin_ctz(vulkanFFTAxis->sampleCount); // Logarithm of base 2
    assert(1 << vulkanFFTAxis->stageCount == vulkanFFTAxis->sampleCount); // Check POT
    vulkanFFTAxis->stageRadix = (uint32_t*)malloc(sizeof(uint32_t) * vulkanFFTAxis->stageCount);
    for(uint32_t j = 0; j < vulkanFFTAxis->stageCount; ++j)
        vulkanFFTAxis->stageRadix[j] = 2;

    {
        vulkanFFTAxis->uboSize = sizeof(UBO) * vulkanFFTAxis->stageCount;
        createBuffer(vulkanFFTPlan->context, &vulkanFFTAxis->ubo, &vulkanFFTAxis->uboDeviceMemory, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT, vulkanFFTAxis->uboSize);
        VulkanFFTTransfer vulkanFFTTransfer;
        vulkanFFTTransfer.context = vulkanFFTPlan->context;
        vulkanFFTTransfer.size = vulkanFFTAxis->uboSize;
        vulkanFFTTransfer.deviceBuffer = vulkanFFTAxis->ubo;
        UBO* ubo = createVulkanFFTUpload(&vulkanFFTTransfer);
        const uint32_t remap[3][3] = {{0, 1, 2}, {1, 2, 0}, {2, 0, 1}};
        uint32_t strides[3] = {1, vulkanFFTPlan->axes[0].sampleCount, vulkanFFTPlan->axes[0].sampleCount * vulkanFFTPlan->axes[1].sampleCount};
        for(uint32_t j = 0; j < vulkanFFTAxis->stageCount; ++j) {
            ubo[j].strideX = strides[remap[axis][0]];
            ubo[j].strideY = strides[remap[axis][1]];
            ubo[j].strideZ = strides[remap[axis][2]];
            ubo[j].radixStride = vulkanFFTAxis->sampleCount / vulkanFFTAxis->stageRadix[j];
            ubo[j].stageSize = 1 << j;
            ubo[j].angleFactor = ((vulkanFFTPlan->inverse) ? -1.0 : 1.0) * M_PI / (float)ubo[j].stageSize;
            ubo[j].normalizationFactor = (vulkanFFTPlan->inverse) ? 1.0 : 1.0 / vulkanFFTAxis->stageRadix[j];
        }
        freeVulkanFFTTransfer(&vulkanFFTTransfer);
    }

    {
        VkDescriptorPoolSize descriptorPoolSize[2] = {};
        descriptorPoolSize[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorPoolSize[0].descriptorCount = vulkanFFTAxis->stageCount;
        descriptorPoolSize[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorPoolSize[1].descriptorCount = vulkanFFTAxis->stageCount*2;
        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
        descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCreateInfo.poolSizeCount = COUNT_OF(descriptorPoolSize);
        descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSize;
        descriptorPoolCreateInfo.maxSets = vulkanFFTAxis->stageCount;
        assert(vkCreateDescriptorPool(vulkanFFTPlan->context->device, &descriptorPoolCreateInfo, vulkanFFTPlan->context->allocator, &vulkanFFTAxis->descriptorPool) == VK_SUCCESS);
    }

    {
        const VkDescriptorType descriptorType[] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
        vulkanFFTAxis->descriptorSetLayouts = (VkDescriptorSetLayout*)malloc(sizeof(VkDescriptorSetLayout) * vulkanFFTAxis->stageCount);
        vulkanFFTAxis->descriptorSets = (VkDescriptorSet*)malloc(sizeof(VkDescriptorSet) * vulkanFFTAxis->stageCount);
        for(uint32_t j = 0; j < vulkanFFTAxis->stageCount; ++j) {
            VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[COUNT_OF(descriptorType)];
            for(uint32_t i = 0; i < COUNT_OF(descriptorSetLayoutBindings); ++i) {
                descriptorSetLayoutBindings[i].binding = i;
                descriptorSetLayoutBindings[i].descriptorType = descriptorType[i];
                descriptorSetLayoutBindings[i].descriptorCount = 1;
                descriptorSetLayoutBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            }
            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
            descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorSetLayoutCreateInfo.bindingCount = COUNT_OF(descriptorSetLayoutBindings);
            descriptorSetLayoutCreateInfo.pBindings = descriptorSetLayoutBindings;
            assert(vkCreateDescriptorSetLayout(vulkanFFTPlan->context->device, &descriptorSetLayoutCreateInfo, vulkanFFTPlan->context->allocator, &vulkanFFTAxis->descriptorSetLayouts[j]) == VK_SUCCESS);
        }
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {};
        descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.descriptorPool = vulkanFFTAxis->descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = vulkanFFTAxis->stageCount;
        descriptorSetAllocateInfo.pSetLayouts = vulkanFFTAxis->descriptorSetLayouts;
        assert(vkAllocateDescriptorSets(vulkanFFTPlan->context->device, &descriptorSetAllocateInfo, vulkanFFTAxis->descriptorSets) == VK_SUCCESS);
        for(uint32_t j = 0; j < vulkanFFTAxis->stageCount; ++j)
            for(uint32_t i = 0; i < COUNT_OF(descriptorType); ++i) {
                VkDescriptorBufferInfo descriptorBufferInfo = {};
                if(i == 0) {
                    descriptorBufferInfo.buffer = vulkanFFTAxis->ubo;
                    descriptorBufferInfo.offset = sizeof(UBO) * j;
                    descriptorBufferInfo.range = sizeof(UBO);
                } else {
                    descriptorBufferInfo.buffer = vulkanFFTPlan->buffer[1 - (vulkanFFTPlan->resultInSwapBuffer + i + j) % 2];
                    descriptorBufferInfo.offset = 0;
                    descriptorBufferInfo.range = vulkanFFTPlan->bufferSize;
                }
                VkWriteDescriptorSet writeDescriptorSet = {};
                writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writeDescriptorSet.dstSet = vulkanFFTAxis->descriptorSets[j];
                writeDescriptorSet.dstBinding = i;
                writeDescriptorSet.dstArrayElement = 0;
                writeDescriptorSet.descriptorType = descriptorType[i];
                writeDescriptorSet.descriptorCount = 1;
                writeDescriptorSet.pBufferInfo = &descriptorBufferInfo;
                vkUpdateDescriptorSets(vulkanFFTPlan->context->device, 1, &writeDescriptorSet, 0, NULL);
            }
    }

    {
        VkPipelineShaderStageCreateInfo pipelineShaderStageCreateInfo = {};
        pipelineShaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineShaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineShaderStageCreateInfo.module = vulkanFFTPlan->context->shaderModule;
        pipelineShaderStageCreateInfo.pName = "main";
        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
        pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.setLayoutCount = vulkanFFTAxis->stageCount;
        pipelineLayoutCreateInfo.pSetLayouts = vulkanFFTAxis->descriptorSetLayouts;
        assert(vkCreatePipelineLayout(vulkanFFTPlan->context->device, &pipelineLayoutCreateInfo, vulkanFFTPlan->context->allocator, &vulkanFFTAxis->pipelineLayout) == VK_SUCCESS);
        VkComputePipelineCreateInfo computePipelineCreateInfo = {};
        computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCreateInfo.stage = pipelineShaderStageCreateInfo;
        computePipelineCreateInfo.layout = vulkanFFTAxis->pipelineLayout;
        assert(vkCreateComputePipelines(vulkanFFTPlan->context->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, vulkanFFTPlan->context->allocator, &vulkanFFTAxis->pipeline) == VK_SUCCESS);
    }

    if(vulkanFFTAxis->stageCount & 1)
        vulkanFFTPlan->resultInSwapBuffer = !vulkanFFTPlan->resultInSwapBuffer;
}

void createVulkanFFT(VulkanFFTPlan* vulkanFFTPlan) {
    vulkanFFTPlan->resultInSwapBuffer = false;
    vulkanFFTPlan->bufferSize = sizeof(complex float) * vulkanFFTPlan->axes[0].sampleCount * vulkanFFTPlan->axes[1].sampleCount * vulkanFFTPlan->axes[2].sampleCount;
    for(uint32_t i = 0; i < COUNT_OF(vulkanFFTPlan->buffer); ++i)
        createBuffer(vulkanFFTPlan->context, &vulkanFFTPlan->buffer[i], &vulkanFFTPlan->deviceMemory[i], VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT, vulkanFFTPlan->bufferSize);
    for(uint32_t i = 0; i < COUNT_OF(vulkanFFTPlan->axes); ++i)
        if(vulkanFFTPlan->axes[i].sampleCount > 1)
            planVulkanFFTAxis(vulkanFFT, i);
}

void recordVulkanFFT(VulkanFFTPlan* vulkanFFTPlan, VkCommandBuffer commandBuffer) {
    const uint32_t remap[3][3] = {{0, 1, 2}, {1, 2, 0}, {2, 0, 1}};
    for(uint32_t i = 0; i < COUNT_OF(vulkanFFTPlan->axes); ++i) {
        if(vulkanFFTPlan->axes[i].sampleCount <= 1)
            continue;
        VulkanFFTAxis* vulkanFFTAxis = &vulkanFFTPlan->axes[i];
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkanFFTAxis->pipeline);
        for(uint32_t j = 0; j < vulkanFFTAxis->stageCount; ++j) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkanFFTAxis->pipelineLayout, 0, 1, &vulkanFFTAxis->descriptorSets[j], 0, NULL);
            vkCmdDispatch(commandBuffer, vulkanFFTAxis->sampleCount / vulkanFFTAxis->stageRadix[j], vulkanFFTPlan->axes[remap[i][1]].sampleCount, vulkanFFTPlan->axes[remap[i][2]].sampleCount);
        }
    }
}

void destroyVulkanFFT(VulkanFFTPlan* vulkanFFTPlan) {
    for(uint32_t i = 0; i < COUNT_OF(vulkanFFTPlan->axes); ++i) {
        if(vulkanFFTPlan->axes[i].sampleCount <= 1)
            continue;
        VulkanFFTAxis* vulkanFFTAxis = &vulkanFFTPlan->axes[i];
        vkDestroyBuffer(vulkanFFTPlan->context->device, vulkanFFTAxis->ubo, vulkanFFTPlan->context->allocator);
        vkFreeMemory(vulkanFFTPlan->context->device, vulkanFFTAxis->uboDeviceMemory, vulkanFFTPlan->context->allocator);
        vkDestroyDescriptorPool(vulkanFFTPlan->context->device, vulkanFFTAxis->descriptorPool, vulkanFFTPlan->context->allocator);
        for(uint32_t j = 0; j < vulkanFFTAxis->stageCount; ++j)
            vkDestroyDescriptorSetLayout(vulkanFFTPlan->context->device, vulkanFFTAxis->descriptorSetLayouts[j], vulkanFFTPlan->context->allocator);
        vkDestroyPipelineLayout(vulkanFFTPlan->context->device, vulkanFFTAxis->pipelineLayout, vulkanFFTPlan->context->allocator);
        vkDestroyPipeline(vulkanFFTPlan->context->device, vulkanFFTAxis->pipeline, vulkanFFTPlan->context->allocator);
        free(vulkanFFTAxis->stageRadix);
        free(vulkanFFTAxis->descriptorSetLayouts);
        free(vulkanFFTAxis->descriptorSets);
    }
    for(uint32_t i = 0; i < COUNT_OF(vulkanFFTPlan->buffer); ++i) {
        vkDestroyBuffer(vulkanFFTPlan->context->device, vulkanFFTPlan->buffer[i], vulkanFFTPlan->context->allocator);
        vkFreeMemory(vulkanFFTPlan->context->device, vulkanFFTPlan->deviceMemory[i], vulkanFFTPlan->context->allocator);
    }
}
