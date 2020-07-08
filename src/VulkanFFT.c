#include "VulkanFFT.h"
#include <stdlib.h>
#include <assert.h>
#define COUNT_OF(array) (sizeof(array) / sizeof(array[0]))

#ifdef WIN32
#define __builtin_clz __lzcnt
#define _USE_MATH_DEFINES
#endif
#include <math.h>

#include "radix2.h"
#include "radix4.h"
#include "radix8.h"
const uint32_t* shaderModuleCode[] = {
    (uint32_t*)radix2_spv,
    (uint32_t*)radix4_spv,
    (uint32_t*)radix8_spv
};
const uint32_t shaderModuleSize[] = {
    sizeof(radix2_spv),
    sizeof(radix4_spv),
    sizeof(radix8_spv)
};
const uint32_t workGroupSize = 32;

void initVulkanFFTContext(VulkanFFTContext* context) {
    vkGetPhysicalDeviceProperties(context->physicalDevice, &context->physicalDeviceProperties);
    vkGetPhysicalDeviceMemoryProperties(context->physicalDevice, &context->physicalDeviceMemoryProperties);
    VkFenceCreateInfo fenceCreateInfo = {0};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = 0;
    assert(vkCreateFence(context->device, &fenceCreateInfo, context->allocator, &context->fence) == VK_SUCCESS);
    for(uint32_t i = 0; i < SUPPORTED_RADIX_LEVELS; ++i)
        context->shaderModules[i] = loadShaderModule(context, shaderModuleCode[i], shaderModuleSize[i]);
    context->uboAlignment = context->physicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
}

void freeVulkanFFTContext(VulkanFFTContext* context) {
    vkDestroyFence(context->device, context->fence, context->allocator);
    for(uint32_t i = 0; i < SUPPORTED_RADIX_LEVELS; ++i)
        vkDestroyShaderModule(context->device, context->shaderModules[i], context->allocator);
}



VkShaderModule loadShaderModule(VulkanFFTContext* context, const uint32_t* code, size_t codeSize) {
    VkShaderModuleCreateInfo shaderModuleCreateInfo = {0};
    shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCreateInfo.codeSize = codeSize;
    shaderModuleCreateInfo.pCode = code;
    VkShaderModule shaderModule;
    assert(vkCreateShaderModule(context->device, &shaderModuleCreateInfo, context->allocator, &shaderModule) == VK_SUCCESS);
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
    VkBufferCreateInfo bufferCreateInfo = {0};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferCreateInfo.queueFamilyIndexCount = COUNT_OF(queueFamilyIndices);
    bufferCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage = usage;
    assert(vkCreateBuffer(context->device, &bufferCreateInfo, context->allocator, buffer) == VK_SUCCESS);
    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(context->device, *buffer, &memoryRequirements);
    VkMemoryAllocateInfo memoryAllocateInfo = {0};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.allocationSize = memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = findMemoryType(context, memoryRequirements.memoryTypeBits, propertyFlags);
    assert(vkAllocateMemory(context->device, &memoryAllocateInfo, context->allocator, deviceMemory) == VK_SUCCESS);
    vkBindBufferMemory(context->device, *buffer, *deviceMemory, 0);
}

VkCommandBuffer createCommandBuffer(VulkanFFTContext* context, VkCommandBufferUsageFlags usageFlags) {
    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {0};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.commandPool = context->commandPool;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer;
    assert(vkAllocateCommandBuffers(context->device, &commandBufferAllocateInfo, &commandBuffer) == VK_SUCCESS);
    VkCommandBufferBeginInfo commandBufferBeginInfo = {0};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBeginInfo.flags = usageFlags;
    assert(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) == VK_SUCCESS);
    return commandBuffer;
}



void bufferTransfer(VulkanFFTContext* context, VkBuffer dstBuffer, VkBuffer srcBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = createCommandBuffer(context, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VkBufferCopy copyRegion = {0};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    assert(vkQueueSubmit(context->queue, 1, &submitInfo, context->fence) == VK_SUCCESS);
    assert(vkWaitForFences(context->device, 1, &context->fence, VK_TRUE, 100000000000) == VK_SUCCESS);
    assert(vkResetFences(context->device, 1, &context->fence) == VK_SUCCESS);
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



typedef struct {
    uint32_t stride[3];
    uint32_t radixStride, stageSize;
    float directionFactor;
    float angleFactor;
    float normalizationFactor;
} VulkanFFTUBO;

typedef struct VulkanFFTAxis VulkanFFTAxis;

void planVulkanFFTAxis(VulkanFFTPlan* vulkanFFTPlan, uint32_t axis) {
    VulkanFFTAxis* vulkanFFTAxis = &vulkanFFTPlan->axes[axis];

    {
        vulkanFFTAxis->stageCount = 31-__builtin_clz(vulkanFFTAxis->sampleCount); // Logarithm of base 2
        vulkanFFTAxis->stageRadix = (uint32_t*)malloc(sizeof(uint32_t) * vulkanFFTAxis->stageCount);
        uint32_t stageSize = vulkanFFTAxis->sampleCount;
        vulkanFFTAxis->stageCount = 0;
        while(stageSize > 1) {
            uint32_t radixIndex = SUPPORTED_RADIX_LEVELS;
            do {
                assert(radixIndex > 0);
                --radixIndex;
                vulkanFFTAxis->stageRadix[vulkanFFTAxis->stageCount] = 2<<radixIndex;
            } while(stageSize % vulkanFFTAxis->stageRadix[vulkanFFTAxis->stageCount] > 0);
            stageSize /= vulkanFFTAxis->stageRadix[vulkanFFTAxis->stageCount];
            ++vulkanFFTAxis->stageCount;
        }
    }

    {
        vulkanFFTAxis->uboSize = vulkanFFTPlan->context->uboAlignment * vulkanFFTAxis->stageCount;
        createBuffer(vulkanFFTPlan->context, &vulkanFFTAxis->ubo, &vulkanFFTAxis->uboDeviceMemory, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT, vulkanFFTAxis->uboSize);
        VulkanFFTTransfer vulkanFFTTransfer;
        vulkanFFTTransfer.context = vulkanFFTPlan->context;
        vulkanFFTTransfer.size = vulkanFFTAxis->uboSize;
        vulkanFFTTransfer.deviceBuffer = vulkanFFTAxis->ubo;
        char* ubo = createVulkanFFTUpload(&vulkanFFTTransfer);
        const uint32_t remap[3][3] = {{0, 1, 2}, {1, 2, 0}, {2, 0, 1}};
        uint32_t strides[3] = {1, vulkanFFTPlan->axes[0].sampleCount, vulkanFFTPlan->axes[0].sampleCount * vulkanFFTPlan->axes[1].sampleCount};
        uint32_t stageSize = 1;
        for(uint32_t j = 0; j < vulkanFFTAxis->stageCount; ++j) {
            VulkanFFTUBO* uboFrame = (VulkanFFTUBO*)&ubo[vulkanFFTPlan->context->uboAlignment * j];
            uboFrame->stride[0] = strides[remap[axis][0]];
            uboFrame->stride[1] = strides[remap[axis][1]];
            uboFrame->stride[2] = strides[remap[axis][2]];
            uboFrame->radixStride = vulkanFFTAxis->sampleCount / vulkanFFTAxis->stageRadix[j];
            uboFrame->stageSize = stageSize;
            uboFrame->directionFactor = (vulkanFFTPlan->inverse) ? -1.0F : 1.0F;
            uboFrame->angleFactor = uboFrame->directionFactor * (float) (M_PI / uboFrame->stageSize);
            uboFrame->normalizationFactor = (vulkanFFTPlan->inverse) ? 1.0F : 1.0F / vulkanFFTAxis->stageRadix[j];
            stageSize *= vulkanFFTAxis->stageRadix[j];
        }
        freeVulkanFFTTransfer(&vulkanFFTTransfer);
    }

    {
        VkDescriptorPoolSize descriptorPoolSize[2] = {0};
        descriptorPoolSize[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorPoolSize[0].descriptorCount = vulkanFFTAxis->stageCount;
        descriptorPoolSize[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorPoolSize[1].descriptorCount = vulkanFFTAxis->stageCount*2;
        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {0};
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
        VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[COUNT_OF(descriptorType)];
        for(uint32_t i = 0; i < COUNT_OF(descriptorSetLayoutBindings); ++i) {
            descriptorSetLayoutBindings[i].binding = i;
            descriptorSetLayoutBindings[i].descriptorType = descriptorType[i];
            descriptorSetLayoutBindings[i].descriptorCount = 1;
            descriptorSetLayoutBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {0};
        descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutCreateInfo.bindingCount = COUNT_OF(descriptorSetLayoutBindings);
        descriptorSetLayoutCreateInfo.pBindings = descriptorSetLayoutBindings;
        assert(vkCreateDescriptorSetLayout(vulkanFFTPlan->context->device, &descriptorSetLayoutCreateInfo, vulkanFFTPlan->context->allocator, &vulkanFFTAxis->descriptorSetLayouts[0]) == VK_SUCCESS);
        for(uint32_t j = 1; j < vulkanFFTAxis->stageCount; ++j)
            vulkanFFTAxis->descriptorSetLayouts[j] = vulkanFFTAxis->descriptorSetLayouts[0];
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {0};
        descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.descriptorPool = vulkanFFTAxis->descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = vulkanFFTAxis->stageCount;
        descriptorSetAllocateInfo.pSetLayouts = vulkanFFTAxis->descriptorSetLayouts;
        assert(vkAllocateDescriptorSets(vulkanFFTPlan->context->device, &descriptorSetAllocateInfo, vulkanFFTAxis->descriptorSets) == VK_SUCCESS);
        for(uint32_t j = 0; j < vulkanFFTAxis->stageCount; ++j)
            for(uint32_t i = 0; i < COUNT_OF(descriptorType); ++i) {
                VkDescriptorBufferInfo descriptorBufferInfo = {0};
                if(i == 0) {
                    descriptorBufferInfo.buffer = vulkanFFTAxis->ubo;
                    descriptorBufferInfo.offset = vulkanFFTPlan->context->uboAlignment * j;
                    descriptorBufferInfo.range = sizeof(VulkanFFTUBO);
                } else {
                    descriptorBufferInfo.buffer = vulkanFFTPlan->buffer[1 - (vulkanFFTPlan->resultInSwapBuffer + i + j) % 2];
                    descriptorBufferInfo.offset = 0;
                    descriptorBufferInfo.range = vulkanFFTPlan->bufferSize;
                }
                VkWriteDescriptorSet writeDescriptorSet = {0};
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
        vulkanFFTAxis->pipelines = (VkPipeline*)malloc(sizeof(VkPipeline) * SUPPORTED_RADIX_LEVELS);
        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {0};
        pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.setLayoutCount = vulkanFFTAxis->stageCount;
        pipelineLayoutCreateInfo.pSetLayouts = vulkanFFTAxis->descriptorSetLayouts;
        assert(vkCreatePipelineLayout(vulkanFFTPlan->context->device, &pipelineLayoutCreateInfo, vulkanFFTPlan->context->allocator, &vulkanFFTAxis->pipelineLayout) == VK_SUCCESS);
        VkPipelineShaderStageCreateInfo pipelineShaderStageCreateInfo[SUPPORTED_RADIX_LEVELS] = {0};
        VkComputePipelineCreateInfo computePipelineCreateInfo[SUPPORTED_RADIX_LEVELS] = {0};
        for(uint32_t i = 0; i < SUPPORTED_RADIX_LEVELS; ++i) {
            pipelineShaderStageCreateInfo[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            pipelineShaderStageCreateInfo[i].stage = VK_SHADER_STAGE_COMPUTE_BIT;
            pipelineShaderStageCreateInfo[i].module = vulkanFFTPlan->context->shaderModules[i];
            pipelineShaderStageCreateInfo[i].pName = "main";
            computePipelineCreateInfo[i].sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            computePipelineCreateInfo[i].stage = pipelineShaderStageCreateInfo[i];
            computePipelineCreateInfo[i].layout = vulkanFFTAxis->pipelineLayout;
        }
        assert(vkCreateComputePipelines(vulkanFFTPlan->context->device, VK_NULL_HANDLE, SUPPORTED_RADIX_LEVELS, computePipelineCreateInfo, vulkanFFTPlan->context->allocator, vulkanFFTAxis->pipelines) == VK_SUCCESS);
    }

    if(vulkanFFTAxis->stageCount & 1)
        vulkanFFTPlan->resultInSwapBuffer = !vulkanFFTPlan->resultInSwapBuffer;
}

void createVulkanFFT(VulkanFFTPlan* vulkanFFTPlan) {
    vulkanFFTPlan->resultInSwapBuffer = false;
    vulkanFFTPlan->bufferSize = sizeof(float) * 2 * vulkanFFTPlan->axes[0].sampleCount * vulkanFFTPlan->axes[1].sampleCount * vulkanFFTPlan->axes[2].sampleCount;
    for(uint32_t i = 0; i < COUNT_OF(vulkanFFTPlan->buffer); ++i)
        createBuffer(vulkanFFTPlan->context, &vulkanFFTPlan->buffer[i], &vulkanFFTPlan->deviceMemory[i], VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT, vulkanFFTPlan->bufferSize);
    for(uint32_t i = 0; i < COUNT_OF(vulkanFFTPlan->axes); ++i)
        if(vulkanFFTPlan->axes[i].sampleCount > 1)
            planVulkanFFTAxis(vulkanFFTPlan, i);
}

void recordVulkanFFT(VulkanFFTPlan* vulkanFFTPlan, VkCommandBuffer commandBuffer) {
    const uint32_t remap[3][3] = {{0, 1, 2}, {1, 2, 0}, {2, 0, 1}};
    for(uint32_t i = 0; i < COUNT_OF(vulkanFFTPlan->axes); ++i) {
        if(vulkanFFTPlan->axes[i].sampleCount <= 1)
            continue;
        VulkanFFTAxis* vulkanFFTAxis = &vulkanFFTPlan->axes[i];
        for(uint32_t j = 0; j < vulkanFFTAxis->stageCount; ++j) {
            uint32_t workGroupCount = vulkanFFTAxis->sampleCount / (vulkanFFTAxis->stageRadix[j] * workGroupSize);
            if(workGroupCount == 0)
                workGroupCount = 1;
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkanFFTAxis->pipelines[30-__builtin_clz(vulkanFFTAxis->stageRadix[j])]);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkanFFTAxis->pipelineLayout, 0, 1, &vulkanFFTAxis->descriptorSets[j], 0, NULL);
            vkCmdDispatch(commandBuffer, workGroupCount, vulkanFFTPlan->axes[remap[i][1]].sampleCount, vulkanFFTPlan->axes[remap[i][2]].sampleCount);
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
        vkDestroyDescriptorSetLayout(vulkanFFTPlan->context->device, vulkanFFTAxis->descriptorSetLayouts[0], vulkanFFTPlan->context->allocator);
        vkDestroyPipelineLayout(vulkanFFTPlan->context->device, vulkanFFTAxis->pipelineLayout, vulkanFFTPlan->context->allocator);
        for(uint32_t j = 0; j < SUPPORTED_RADIX_LEVELS; ++j)
            vkDestroyPipeline(vulkanFFTPlan->context->device, vulkanFFTAxis->pipelines[j], vulkanFFTPlan->context->allocator);
        free(vulkanFFTAxis->pipelines);
        free(vulkanFFTAxis->stageRadix);
        free(vulkanFFTAxis->descriptorSetLayouts);
        free(vulkanFFTAxis->descriptorSets);
    }
    for(uint32_t i = 0; i < COUNT_OF(vulkanFFTPlan->buffer); ++i) {
        vkDestroyBuffer(vulkanFFTPlan->context->device, vulkanFFTPlan->buffer[i], vulkanFFTPlan->context->allocator);
        vkFreeMemory(vulkanFFTPlan->context->device, vulkanFFTPlan->deviceMemory[i], vulkanFFTPlan->context->allocator);
    }
}
