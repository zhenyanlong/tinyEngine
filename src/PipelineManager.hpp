#pragma once
#include "VulkanContext.hpp"
#include "RenderPassManager.hpp"
#include <string>
#include <vector>
#include <unordered_map>

// Independent enum to avoid a circular include with MaterialManager.
// MaterialManager will translate its own MaterialType into this when
// requesting a pipeline.
enum class PipelineVariant { Mesh, Box };

class PipelineManager {
public:
    void create(const VulkanContext& ctx, const RenderPassManager& rpMgr,
                const std::string& vertSpv, const std::string& fragSpv,
                const std::string& boxVertSpv, const std::string& boxFragSpv,
                VkExtent2D extent);
    void recreate(const VulkanContext& ctx, const RenderPassManager& rpMgr,
                  const std::string& vertSpv, const std::string& fragSpv,
                  const std::string& boxVertSpv, const std::string& boxFragSpv,
                  VkExtent2D extent);
    void destroy(const VulkanContext& ctx);

    VkDescriptorSetLayout getMainDescSetLayout() const { return mainDescSetLayout_; }
    VkDescriptorSetLayout getBoxDescSetLayout()  const { return boxDescSetLayout_; }
    VkDescriptorSetLayout getSkinnedDescSetLayout() const { return skinnedDescSetLayout_; }
    VkPipelineLayout      getMainPipelineLayout() const { return mainPipelineLayout_; }
    VkPipelineLayout      getBoxPipelineLayout()  const { return boxPipelineLayout_; }
    VkPipelineLayout      getPickPipelineLayout() const { return pickPipelineLayout_; }
    VkPipelineLayout      getSkinnedPickPipelineLayout() const { return skinnedPickPipelineLayout_; }
    VkPipelineLayout      getSkinnedPipelineLayout() const { return skinnedPipelineLayout_; }
    VkPipeline            getMainPipeline()       const { return mainPipeline_; }
    VkPipeline            getBoxPipeline()        const { return boxPipeline_; }
    VkPipeline            getPickPipeline()       const { return pickPipeline_; }
    VkPipeline            getSkinnedPickPipeline() const { return skinnedPickPipeline_; }
    VkPipeline            getSkinnedPipeline()    const { return skinnedMeshPipeline_; }

    // Returns a pipeline matching (variant, vertSpvPath, fragSpvPath).
    // Empty paths -> returns the corresponding default pipeline.
    // Pipelines are cached internally and owned by PipelineManager.
    // On any error (file missing, creation failed) the default pipeline
    // is returned and a warning is printed to stderr.
    VkPipeline acquirePipeline(const VulkanContext& ctx,
                               PipelineVariant variant,
                               const std::string& vertSpvPath,
                               const std::string& fragSpvPath);

    static std::vector<char> readFile(const std::string& path);
    VkShaderModule createShaderModule(const VulkanContext& ctx, const std::vector<char>& code) const;

    void destroyPipelines(const VulkanContext& ctx);

private:
    VkDescriptorSetLayout mainDescSetLayout_{};
    VkDescriptorSetLayout boxDescSetLayout_{};
    VkDescriptorSetLayout skinnedDescSetLayout_{};
    VkPipelineLayout      mainPipelineLayout_{};
    VkPipelineLayout      boxPipelineLayout_{};
    VkPipelineLayout      pickPipelineLayout_{};
    VkPipelineLayout      skinnedPickPipelineLayout_{};
    VkPipelineLayout      skinnedPipelineLayout_{};
    VkPipeline            mainPipeline_{};
    VkPipeline            boxPipeline_{};
    VkPipeline            pickPipeline_{};
    VkPipeline            skinnedPickPipeline_{};
    VkPipeline            skinnedMeshPipeline_{};

    // Dynamic pipeline cache keyed by "variant|vert|frag".
    std::unordered_map<std::string, VkPipeline> dynamicPipelines_;
    VkRenderPass cachedMainRenderPass_{};
    VkExtent2D   cachedExtent_{0, 0};

    void createDescriptorSetLayouts(const VulkanContext& ctx);
    void createMainPipeline(const VulkanContext& ctx, VkRenderPass renderPass,
                            const std::string& vertSpv, const std::string& fragSpv,
                            VkExtent2D extent);
    void createBoxPipeline(const VulkanContext& ctx, VkRenderPass renderPass,
                           const std::string& vertSpv, const std::string& fragSpv,
                           VkExtent2D extent);
    void createPickPipeline(const VulkanContext& ctx, VkRenderPass pickRenderPass,
                            const std::string& vertSpv, VkExtent2D extent);
    void createSkinnedPipeline(const VulkanContext& ctx, VkRenderPass renderPass,
                               const std::string& skinnedVertSpv, const std::string& fragSpv,
                               VkExtent2D extent);

    // Build VkPipeline only (layout reused). Returns VK_NULL_HANDLE on failure.
    VkPipeline buildMainPipeline(const VulkanContext& ctx, VkRenderPass renderPass,
                                 const std::string& vertSpv, const std::string& fragSpv,
                                 VkExtent2D extent);
    VkPipeline buildBoxPipeline(const VulkanContext& ctx, VkRenderPass renderPass,
                                const std::string& vertSpv, const std::string& fragSpv,
                                VkExtent2D extent);
    VkPipeline buildSkinnedPipeline(const VulkanContext& ctx, VkRenderPass renderPass,
                                    const std::string& vertSpv, const std::string& fragSpv,
                                    VkExtent2D extent);
};
