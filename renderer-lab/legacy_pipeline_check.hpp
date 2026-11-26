#pragma once
#include "legacy_shader_program.hpp"
#include "legacy_shader_dxc.hpp"
#include <vulkan/vulkan.h>
#include <spirv_reflect.h>
#include <map>

namespace legacy_pipeline {

using movie_pass::Com;
using movie_pass::Require;

class D3D12 {
public:
    D3D12(IDXGIAdapter1* adapter) {
        Require(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), device.AsVoid()), "D3D12 device");
        D3D12_DESCRIPTOR_RANGE ranges[6]{};
        D3D12_ROOT_PARAMETER params[6]{};
        constexpr D3D12_DESCRIPTOR_RANGE_TYPE classes[] = {D3D12_DESCRIPTOR_RANGE_TYPE_CBV, D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                                                            D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER};
        for (unsigned i = 0; i < 6; ++i) {
            auto& r = ranges[i];
            r.RangeType = classes[i % 3];
            r.NumDescriptors = 16;
            params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[i].ShaderVisibility = i < 3 ? D3D12_SHADER_VISIBILITY_VERTEX : D3D12_SHADER_VISIBILITY_PIXEL;
            params[i].DescriptorTable = {1, &r};
        }
        D3D12_ROOT_SIGNATURE_DESC desc{6, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        Com<ID3DBlob> blob, errors;
        Require(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, blob.Put(), errors.Put()), "Serialize material roots");
        Require(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            __uuidof(ID3D12RootSignature), root.AsVoid()), "Create D3D12 material roots");
    }

    void Create(const std::string& vertexHlsl, IDxcBlob* vs, IDxcBlob* ps) {
        auto source = legacy_program::detail::ParseLayout(vertexHlsl, "UnlimitedShader_Input");
        std::vector<D3D12_INPUT_ELEMENT_DESC> inputs;
        for (auto& field : source.fields) {
            const auto digit = field.semantic.find_first_of("0123456789");
            const UINT index = digit == std::string::npos ? 0 : std::stoul(field.semantic.substr(digit));
            if (digit != std::string::npos) field.semantic.resize(digit);
            if (field.type != "float4") throw std::runtime_error("Unsupported diagnostic input format " + field.type);
            inputs.push_back({field.semantic.c_str(), index, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, UINT(inputs.size() * 16),
                              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = root.Get();
        pipeline.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pipeline.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pipeline.InputLayout = {inputs.data(), UINT(inputs.size())};
        pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline.SampleMask = UINT_MAX;
        pipeline.SampleDesc.Count = 1;
        pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipeline.RasterizerState.DepthClipEnable = TRUE;
        pipeline.NumRenderTargets = 1;
        pipeline.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        Com<ID3D12PipelineState> object;
        Require(device->CreateGraphicsPipelineState(&pipeline, __uuidof(ID3D12PipelineState), object.AsVoid()), "Create material D3D12 PSO");
    }
private:
    Com<ID3D12Device> device;
    Com<ID3D12RootSignature> root;
};

inline void RequireVk(VkResult status, const char* name) {
    if (status != VK_SUCCESS) throw std::runtime_error(std::string(name) + " VkResult " + std::to_string(status));
}

struct VkHost {
    VkInstance instance{};
    VkHost() {
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "Unlimited material diagnostic";
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &app;
        RequireVk(vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance");
    }
    ~VkHost() { if (instance) vkDestroyInstance(instance, nullptr); }
    VkHost(const VkHost&) = delete;
};

struct ReflectedModule {
    SpvReflectShaderModule module{};
    explicit ReflectedModule(IDxcBlob* spv) {
        if (spvReflectCreateShaderModule(spv->GetBufferSize(), spv->GetBufferPointer(), &module) != SPV_REFLECT_RESULT_SUCCESS)
            throw std::runtime_error("SPIR-V reflection failed");
    }
    ~ReflectedModule() { spvReflectDestroyShaderModule(&module); }
    ReflectedModule(const ReflectedModule&) = delete;
};

class Vulkan {
public:
    explicit Vulkan(VkPhysicalDevice gpu) {
        unsigned families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, nullptr);
        std::vector<VkQueueFamilyProperties> available(families);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, available.data());
        auto found = std::find_if(available.begin(), available.end(), [](const auto& item) {
            return item.queueCount && (item.queueFlags & VK_QUEUE_GRAPHICS_BIT);
        });
        if (found == available.end()) throw std::runtime_error("No graphics queue");
        float weight = 1;
        VkDeviceQueueCreateInfo queue{};
        queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex = static_cast<UINT>(found - available.begin());
        queue.queueCount = 1;
        queue.pQueuePriorities = &weight;
        VkDeviceCreateInfo setup{};
        setup.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        setup.queueCreateInfoCount = 1;
        setup.pQueueCreateInfos = &queue;
        RequireVk(vkCreateDevice(gpu, &setup, nullptr, &device), "vkCreateDevice");
    }
    Vulkan(const Vulkan&) = delete;
    ~Vulkan() { vkDestroyDevice(device, nullptr); }

    void Create(IDxcBlob* vs, IDxcBlob* ps) {
        const ReflectedModule v(vs), p(ps);
        std::vector<VkDescriptorSetLayoutBinding> descriptors;
        for (const auto* stage : {&v.module, &p.module}) {
            for (unsigned i = 0; i < stage->descriptor_binding_count; ++i) {
                const auto& b = stage->descriptor_bindings[i];
                if (b.set || !b.count) throw std::runtime_error("Unexpected Vulkan descriptor set or array");
                for (const auto& previous : descriptors) {
                    if (previous.binding == b.binding) throw std::runtime_error("Vulkan descriptor binding collision");
                }
                descriptors.push_back({b.binding, static_cast<VkDescriptorType>(b.descriptor_type), b.count,
                                       static_cast<VkShaderStageFlags>(stage->shader_stage), nullptr});
            }
        }
        std::vector<VkVertexInputAttributeDescription> attrs;
        for (unsigned i = 0; i < v.module.input_variable_count; ++i) {
            auto& a = *v.module.input_variables[i];
            if (a.decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) continue;
            attrs.push_back({a.location, 0, static_cast<VkFormat>(a.format), UINT(attrs.size() * 16)});
        }
        for (unsigned i = 0; i < p.module.input_variable_count; ++i) {
            const auto& input = *p.module.input_variables[i];
            if (input.decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) continue;
            if (input.location == UINT_MAX) throw std::runtime_error("Fragment input without location");
            bool compatible = false;
            for (unsigned j = 0; j < v.module.output_variable_count; ++j) {
                const auto& out = *v.module.output_variables[j];
                if (!(out.decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) && out.location == input.location &&
                    out.format == input.format && out.name && input.name) {
                    const std::string outName(out.name), inName(input.name);
                    compatible = outName.substr(outName.find_last_of('.') + 1) == inName.substr(inName.find_last_of('.') + 1);
                    break;
                }
            }
            if (!compatible) throw std::runtime_error("Vulkan stage interface mismatch: " + std::string(input.name ? input.name : "?"));
        }

        struct Handles {
            VkDevice d;
            VkDescriptorSetLayout set{};
            VkPipelineLayout layout{};
            VkRenderPass pass{};
            VkShaderModule modules[2]{};
            VkPipeline pipeline{};
            ~Handles() {
                vkDestroyPipeline(d, pipeline, nullptr);
                for (auto m : modules) vkDestroyShaderModule(d, m, nullptr);
                vkDestroyRenderPass(d, pass, nullptr);
                vkDestroyPipelineLayout(d, layout, nullptr);
                vkDestroyDescriptorSetLayout(d, set, nullptr);
            }
        } handles{device};
        VkDescriptorSetLayoutCreateInfo set{};
        set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set.bindingCount = UINT(descriptors.size());
        set.pBindings = descriptors.data();
        RequireVk(vkCreateDescriptorSetLayout(device, &set, nullptr, &handles.set), "vkCreateDescriptorSetLayout");
        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &handles.set;
        RequireVk(vkCreatePipelineLayout(device, &layout, nullptr, &handles.layout), "vkCreatePipelineLayout");
        VkAttachmentDescription color{};
        color.format = VK_FORMAT_B8G8R8A8_UNORM;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;
        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = rp.subpassCount = 1;
        rp.pAttachments = &color;
        rp.pSubpasses = &sub;
        RequireVk(vkCreateRenderPass(device, &rp, nullptr, &handles.pass), "vkCreateRenderPass");
        VkPipelineShaderStageCreateInfo shaders[2]{};
        for (unsigned i = 0; i < 2; ++i) {
            auto* blob = i ? ps : vs;
            VkShaderModuleCreateInfo m{};
            m.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            m.codeSize = blob->GetBufferSize();
            m.pCode = static_cast<const uint32_t*>(blob->GetBufferPointer());
            RequireVk(vkCreateShaderModule(device, &m, nullptr, &handles.modules[i]), "vkCreateShaderModule");
            shaders[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaders[i].module = handles.modules[i];
            shaders[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
            shaders[i].pName = "UnlimitedShader";
        }
        VkVertexInputBindingDescription binding{0, UINT(attrs.size() * 16), VK_VERTEX_INPUT_RATE_VERTEX};
        VkPipelineVertexInputStateCreateInfo vertices{};
        vertices.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertices.vertexBindingDescriptionCount = attrs.empty() ? 0 : 1;
        vertices.pVertexBindingDescriptions = &binding;
        vertices.vertexAttributeDescriptionCount = UINT(attrs.size());
        vertices.pVertexAttributeDescriptions = attrs.data();
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0,0,16,16,0,1};
        VkRect2D scissor{{0,0},{16,16}};
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = vp.scissorCount = 1;
        vp.pViewports = &viewport;
        vp.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{};
        samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState target{};
        target.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &target;
        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2;
        info.pStages = shaders;
        info.pVertexInputState = &vertices;
        info.pInputAssemblyState = &ia;
        info.pViewportState = &vp;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &samples;
        info.pColorBlendState = &blend;
        info.renderPass = handles.pass;
        info.layout = handles.layout;
        RequireVk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &handles.pipeline), "vkCreateGraphicsPipelines");
    }
private:
    VkDevice device{};
};

} // namespace legacy_pipeline

