#include "descriptor_set.hpp"
#include "logger.hpp"

namespace vkBasalt
{

    VkDescriptorPool createDescriptorPool(LogicalDevice* pLogicalDevice, const std::vector<VkDescriptorPoolSize>& poolSizes)
    {
        std::vector<VkDescriptorPoolSize> nonZeroPoolSizes;
        nonZeroPoolSizes.reserve(poolSizes.size());
        for (const VkDescriptorPoolSize& poolSize : poolSizes)
        {
            if (poolSize.descriptorCount != 0)
                nonZeroPoolSizes.push_back(poolSize);
        }

        uint32_t setCount = 0;

        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        for (const VkDescriptorPoolSize& poolSize : nonZeroPoolSizes)
        {
            setCount += poolSize.descriptorCount;
        }
        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo;
        descriptorPoolCreateInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCreateInfo.pNext         = nullptr;
        descriptorPoolCreateInfo.flags         = 0;
        descriptorPoolCreateInfo.maxSets       = setCount > 0 ? setCount : 1;
        descriptorPoolCreateInfo.poolSizeCount = nonZeroPoolSizes.size();
        descriptorPoolCreateInfo.pPoolSizes    = nonZeroPoolSizes.empty() ? nullptr : nonZeroPoolSizes.data();

        VkResult result = pLogicalDevice->vkd.CreateDescriptorPool(pLogicalDevice->device, &descriptorPoolCreateInfo, nullptr, &descriptorPool);
        ASSERT_VULKAN(result);
        return descriptorPool;
    }

    VkDescriptorSetLayout createUniformBufferDescriptorSetLayout(LogicalDevice* pLogicalDevice)
    {
        VkDescriptorSetLayout descriptorSetLayout;

        VkDescriptorSetLayoutBinding descriptorSetLayoutBinding;
        descriptorSetLayoutBinding.binding            = 0;
        descriptorSetLayoutBinding.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorSetLayoutBinding.descriptorCount    = 1;
        descriptorSetLayoutBinding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
        descriptorSetLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo descriptorSetCreateInfo;
        descriptorSetCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetCreateInfo.pNext        = nullptr;
        descriptorSetCreateInfo.flags        = 0;
        descriptorSetCreateInfo.bindingCount = 1;
        descriptorSetCreateInfo.pBindings    = &descriptorSetLayoutBinding;

        VkResult result =
            pLogicalDevice->vkd.CreateDescriptorSetLayout(pLogicalDevice->device, &descriptorSetCreateInfo, nullptr, &descriptorSetLayout);
        ASSERT_VULKAN(result)

        return descriptorSetLayout;
    }

    std::vector<VkDescriptorSet> allocateAndWriteBufferDescriptorSets(
        LogicalDevice* pLogicalDevice, VkDescriptorPool descriptorPool,
        VkDescriptorSetLayout descriptorSetLayout,
        const std::vector<VkBuffer>& buffers)
    {
        if (buffers.empty())
            return {};

        std::vector<VkDescriptorSet> descriptorSets(
            buffers.size(), VK_NULL_HANDLE);
        std::vector<VkDescriptorSetLayout> layouts(
            buffers.size(), descriptorSetLayout);
        VkDescriptorSetAllocateInfo allocateInfo = {};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = descriptorPool;
        allocateInfo.descriptorSetCount =
            static_cast<uint32_t>(descriptorSets.size());
        allocateInfo.pSetLayouts = layouts.data();

        const VkResult result = pLogicalDevice->vkd.AllocateDescriptorSets(
            pLogicalDevice->device, &allocateInfo, descriptorSets.data());
        ASSERT_VULKAN(result);

        std::vector<VkDescriptorBufferInfo> bufferInfos(buffers.size());
        std::vector<VkWriteDescriptorSet> writes(buffers.size());
        for (size_t i = 0; i < buffers.size(); ++i)
        {
            bufferInfos[i].buffer = buffers[i];
            bufferInfos[i].offset = 0;
            bufferInfos[i].range = VK_WHOLE_SIZE;

            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = descriptorSets[i];
            writes[i].dstBinding = 0;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[i].pBufferInfo = &bufferInfos[i];
        }
        pLogicalDevice->vkd.UpdateDescriptorSets(
            pLogicalDevice->device, static_cast<uint32_t>(writes.size()),
            writes.data(), 0, nullptr);
        return descriptorSets;
    }

    VkDescriptorSetLayout createImageSamplerDescriptorSetLayout(LogicalDevice* pLogicalDevice, uint32_t count)
    {
        VkDescriptorSetLayout descriptorSetLayout;

        std::vector<VkDescriptorSetLayoutBinding> bindigs(count);
        for (uint32_t i = 0; i < count; i++)
        {
            VkDescriptorSetLayoutBinding descriptorSetLayoutBinding;
            descriptorSetLayoutBinding.binding            = i;
            descriptorSetLayoutBinding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorSetLayoutBinding.descriptorCount    = 1;
            descriptorSetLayoutBinding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
            descriptorSetLayoutBinding.pImmutableSamplers = nullptr;
            bindigs[i]                                    = descriptorSetLayoutBinding;
        }

        VkDescriptorSetLayoutCreateInfo descriptorSetCreateInfo;
        descriptorSetCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetCreateInfo.pNext        = nullptr;
        descriptorSetCreateInfo.flags        = 0;
        descriptorSetCreateInfo.bindingCount = count;
        descriptorSetCreateInfo.pBindings    = bindigs.data();

        VkResult result =
            pLogicalDevice->vkd.CreateDescriptorSetLayout(pLogicalDevice->device, &descriptorSetCreateInfo, nullptr, &descriptorSetLayout);
        ASSERT_VULKAN(result)
        return descriptorSetLayout;
    }

    VkDescriptorSetLayout createStorageImageDescriptorSetLayout(LogicalDevice* pLogicalDevice, uint32_t count)
    {
        VkDescriptorSetLayout descriptorSetLayout;

        std::vector<VkDescriptorSetLayoutBinding> bindings(count);
        for (uint32_t i = 0; i < count; i++)
        {
            VkDescriptorSetLayoutBinding descriptorSetLayoutBinding;
            descriptorSetLayoutBinding.binding            = i;
            descriptorSetLayoutBinding.descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            descriptorSetLayoutBinding.descriptorCount    = 1;
            descriptorSetLayoutBinding.stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
            descriptorSetLayoutBinding.pImmutableSamplers = nullptr;
            bindings[i]                                   = descriptorSetLayoutBinding;
        }

        VkDescriptorSetLayoutCreateInfo descriptorSetCreateInfo;
        descriptorSetCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetCreateInfo.pNext        = nullptr;
        descriptorSetCreateInfo.flags        = 0;
        descriptorSetCreateInfo.bindingCount = count;
        descriptorSetCreateInfo.pBindings    = bindings.data();

        VkResult result =
            pLogicalDevice->vkd.CreateDescriptorSetLayout(pLogicalDevice->device, &descriptorSetCreateInfo, nullptr, &descriptorSetLayout);
        ASSERT_VULKAN(result)
        return descriptorSetLayout;
    }

    std::vector<VkDescriptorSet> allocateAndWriteStorageImageDescriptorSets(LogicalDevice*                        pLogicalDevice,
                                                                            VkDescriptorPool                      descriptorPool,
                                                                            VkDescriptorSetLayout                 descriptorSetLayout,
                                                                            std::vector<std::vector<VkImageView>> imageViewsVectors)
    {
        if (imageViewsVectors.empty() || imageViewsVectors[0].empty())
        {
            Logger::warn("allocateAndWriteStorageImageDescriptorSets: empty imageViewsVectors");
            return {};
        }

        std::vector<VkDescriptorSet>       descriptorSets(imageViewsVectors[0].size());
        std::vector<VkDescriptorSetLayout> layouts(descriptorSets.size(), descriptorSetLayout);

        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo;
        descriptorSetAllocateInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.pNext              = nullptr;
        descriptorSetAllocateInfo.descriptorPool     = descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = descriptorSets.size();
        descriptorSetAllocateInfo.pSetLayouts        = layouts.data();

        VkResult result = pLogicalDevice->vkd.AllocateDescriptorSets(pLogicalDevice->device, &descriptorSetAllocateInfo, descriptorSets.data());
        ASSERT_VULKAN(result);

        for (uint32_t i = 0; i < descriptorSets.size(); i++)
        {
            std::vector<VkDescriptorImageInfo> imageInfos(imageViewsVectors.size());
            std::vector<VkWriteDescriptorSet>  writes(imageViewsVectors.size());

            for (uint32_t j = 0; j < imageViewsVectors.size(); j++)
            {
                imageInfos[j].sampler     = VK_NULL_HANDLE;
                imageInfos[j].imageView   = imageViewsVectors[j][i];
                imageInfos[j].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                writes[j].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[j].pNext            = nullptr;
                writes[j].dstSet           = descriptorSets[i];
                writes[j].dstBinding       = j;
                writes[j].dstArrayElement  = 0;
                writes[j].descriptorCount  = 1;
                writes[j].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                writes[j].pImageInfo       = &imageInfos[j];
                writes[j].pBufferInfo      = nullptr;
                writes[j].pTexelBufferView = nullptr;
            }

            pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, writes.size(), writes.data(), 0, nullptr);
        }

        return descriptorSets;
    }

    std::vector<VkDescriptorSet> allocateAndWriteImageSamplerDescriptorSets(LogicalDevice*                        pLogicalDevice,
                                                                            VkDescriptorPool                      descriptorPool,
                                                                            VkDescriptorSetLayout                 descriptorSetLayout,
                                                                            std::vector<VkSampler>                samplers,
                                                                            std::vector<std::vector<VkImageView>> imageViewsVectors)
    {
        if (imageViewsVectors.empty() || imageViewsVectors[0].empty())
        {
            Logger::warn("allocateAndWriteImageSamplerDescriptorSets: empty imageViewsVectors");
            return {};
        }
        std::vector<VkDescriptorSet> descriptorSets(imageViewsVectors[0].size());

        std::vector<VkDescriptorSetLayout> layouts(descriptorSets.size(), descriptorSetLayout);
        VkDescriptorSetAllocateInfo        descriptorSetAllocateInfo;
        descriptorSetAllocateInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.pNext              = nullptr;
        descriptorSetAllocateInfo.descriptorPool     = descriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = descriptorSets.size();
        descriptorSetAllocateInfo.pSetLayouts        = layouts.data();

        Logger::debug("before allocating descriptor Sets");
        VkResult result = pLogicalDevice->vkd.AllocateDescriptorSets(pLogicalDevice->device, &descriptorSetAllocateInfo, descriptorSets.data());
        ASSERT_VULKAN(result);

        VkDescriptorImageInfo imageInfo;
        imageInfo.sampler     = VK_NULL_HANDLE;
        imageInfo.imageView   = VK_NULL_HANDLE;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        std::vector<VkDescriptorImageInfo> imageInfos(imageViewsVectors.size(), imageInfo);

        VkWriteDescriptorSet writeDescriptorSet = {};

        writeDescriptorSet.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.pNext            = nullptr;
        writeDescriptorSet.dstSet           = VK_NULL_HANDLE;
        writeDescriptorSet.dstBinding       = 0;
        writeDescriptorSet.dstArrayElement  = 0;
        writeDescriptorSet.descriptorCount  = 1;
        writeDescriptorSet.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.pImageInfo       = nullptr;
        writeDescriptorSet.pBufferInfo      = nullptr;
        writeDescriptorSet.pTexelBufferView = nullptr;

        std::vector<VkWriteDescriptorSet> writeDescriptorSets(imageViewsVectors.size(), writeDescriptorSet);

        for (unsigned int i = 0; i < descriptorSets.size(); i++)
        {
            for (uint32_t j = 0; j < imageViewsVectors.size(); j++)
            {
                imageInfos[j].sampler   = samplers[j];
                imageInfos[j].imageView = imageViewsVectors[j][i];

                writeDescriptorSets[j].dstBinding = j;
                writeDescriptorSets[j].pImageInfo = &imageInfos[j];
                writeDescriptorSets[j].dstSet     = descriptorSets[i];
            }
            Logger::debug("before writing descriptor Sets");
            pLogicalDevice->vkd.UpdateDescriptorSets(pLogicalDevice->device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);
        }
        return descriptorSets;
    }
} // namespace vkBasalt
