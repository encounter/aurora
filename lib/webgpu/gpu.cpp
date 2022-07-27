#include "gpu.hpp"

#include <aurora/aurora.h>

#include "../window.hpp"
#include "../internal.hpp"

#include <SDL.h>
#include <dawn/native/DawnNative.h>
#include <magic_enum.hpp>
#include <memory>
#include <algorithm>

#include "../dawn/BackendBinding.hpp"

namespace aurora::webgpu {
static Module Log("aurora::gpu");

WGPUDevice g_device;
WGPUQueue g_queue;
WGPUSwapChain g_swapChain;
WGPUBackendType g_backendType;
GraphicsConfig g_graphicsConfig;
TextureWithSampler g_frameBuffer;
TextureWithSampler g_frameBufferResolved;
TextureWithSampler g_depthBuffer;

// EFB -> XFB copy pipeline
static WGPUBindGroupLayout g_CopyBindGroupLayout;
WGPURenderPipeline g_CopyPipeline;
WGPUBindGroup g_CopyBindGroup;

static std::unique_ptr<dawn::native::Instance> g_Instance;
static dawn::native::Adapter g_Adapter;
static WGPUAdapterProperties g_AdapterProperties;
static std::unique_ptr<utils::BackendBinding> g_BackendBinding;

TextureWithSampler create_render_texture(bool multisampled) {
  const WGPUExtent3D size{
      .width = g_graphicsConfig.width,
      .height = g_graphicsConfig.height,
      .depthOrArrayLayers = 1,
  };
  const auto format = g_graphicsConfig.colorFormat;
  uint32_t sampleCount = 1;
  if (multisampled) {
    sampleCount = g_graphicsConfig.msaaSamples;
  }
  const WGPUTextureDescriptor textureDescriptor{
      .label = "Render texture",
      .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc |
               WGPUTextureUsage_CopyDst,
      .dimension = WGPUTextureDimension_2D,
      .size = size,
      .format = format,
      .mipLevelCount = 1,
      .sampleCount = sampleCount,
  };
  auto texture = wgpuDeviceCreateTexture(g_device, &textureDescriptor);

  const WGPUTextureViewDescriptor viewDescriptor{
      .dimension = WGPUTextureViewDimension_2D,
      .mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED,
      .arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED,
  };
  auto view = wgpuTextureCreateView(texture, &viewDescriptor);

  const WGPUSamplerDescriptor samplerDescriptor{
      .label = "Render sampler",
      .addressModeU = WGPUAddressMode_ClampToEdge,
      .addressModeV = WGPUAddressMode_ClampToEdge,
      .addressModeW = WGPUAddressMode_ClampToEdge,
      .magFilter = WGPUFilterMode_Linear,
      .minFilter = WGPUFilterMode_Linear,
      .mipmapFilter = WGPUFilterMode_Linear,
      .lodMinClamp = 0.f,
      .lodMaxClamp = 1000.f,
      .maxAnisotropy = 1,
  };
  auto sampler = wgpuDeviceCreateSampler(g_device, &samplerDescriptor);

  return {
      .texture{texture},
      .view{view},
      .size = size,
      .format = format,
      .sampler{sampler},
  };
}

static TextureWithSampler create_depth_texture() {
  const WGPUExtent3D size{
      .width = g_graphicsConfig.width,
      .height = g_graphicsConfig.height,
      .depthOrArrayLayers = 1,
  };
  const auto format = g_graphicsConfig.depthFormat;
  const WGPUTextureDescriptor textureDescriptor{
      .label = "Depth texture",
      .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
      .dimension = WGPUTextureDimension_2D,
      .size = size,
      .format = format,
      .mipLevelCount = 1,
      .sampleCount = g_graphicsConfig.msaaSamples,
  };
  auto texture = wgpuDeviceCreateTexture(g_device, &textureDescriptor);

  const WGPUTextureViewDescriptor viewDescriptor{
      .dimension = WGPUTextureViewDimension_2D,
      .mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED,
      .arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED,
  };
  auto view = wgpuTextureCreateView(texture, &viewDescriptor);

  const WGPUSamplerDescriptor samplerDescriptor{
      .label = "Depth sampler",
      .addressModeU = WGPUAddressMode_ClampToEdge,
      .addressModeV = WGPUAddressMode_ClampToEdge,
      .addressModeW = WGPUAddressMode_ClampToEdge,
      .magFilter = WGPUFilterMode_Linear,
      .minFilter = WGPUFilterMode_Linear,
      .mipmapFilter = WGPUFilterMode_Linear,
      .lodMinClamp = 0.f,
      .lodMaxClamp = 1000.f,
      .maxAnisotropy = 1,
  };
  auto sampler = wgpuDeviceCreateSampler(g_device, &samplerDescriptor);

  return {
      .texture{texture},
      .view{view},
      .size = size,
      .format = format,
      .sampler{sampler},
  };
}

void create_copy_pipeline() {
  WGPUShaderModuleWGSLDescriptor sourceDescriptor{
      .chain = {.sType = WGPUSType_ShaderModuleWGSLDescriptor},
      .source = R"""(
@group(0) @binding(0)
var efb_sampler: sampler;
@group(0) @binding(1)
var efb_texture: texture_2d<f32>;

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

var<private> pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
    vec2(-1.0, 1.0),
    vec2(-1.0, -3.0),
    vec2(3.0, 1.0),
);
var<private> uvs: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
    vec2(0.0, 0.0),
    vec2(0.0, 2.0),
    vec2(2.0, 0.0),
);

@stage(vertex)
fn vs_main(@builtin(vertex_index) vtxIdx: u32) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4<f32>(pos[vtxIdx], 0.0, 1.0);
    out.uv = uvs[vtxIdx];
    return out;
}

@stage(fragment)
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    return textureSample(efb_texture, efb_sampler, in.uv);
}
)""",
  };
  const WGPUShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &sourceDescriptor.chain,
      .label = "XFB Copy Module",
  };
  auto module = wgpuDeviceCreateShaderModule(g_device, &moduleDescriptor);
  const std::array colorTargets{WGPUColorTargetState{
      .format = g_graphicsConfig.colorFormat,
      .writeMask = WGPUColorWriteMask_All,
  }};
  const WGPUFragmentState fragmentState{
      .module = module,
      .entryPoint = "fs_main",
      .targetCount = colorTargets.size(),
      .targets = colorTargets.data(),
  };
  const std::array bindGroupLayoutEntries{
      WGPUBindGroupLayoutEntry{
          .binding = 0,
          .visibility = WGPUShaderStage_Fragment,
          .sampler =
              WGPUSamplerBindingLayout{
                  .type = WGPUSamplerBindingType_Filtering,
              },
      },
      WGPUBindGroupLayoutEntry{
          .binding = 1,
          .visibility = WGPUShaderStage_Fragment,
          .texture =
              WGPUTextureBindingLayout{
                  .sampleType = WGPUTextureSampleType_Float,
                  .viewDimension = WGPUTextureViewDimension_2D,
              },
      },
  };
  const WGPUBindGroupLayoutDescriptor bindGroupLayoutDescriptor{
      .entryCount = bindGroupLayoutEntries.size(),
      .entries = bindGroupLayoutEntries.data(),
  };
  g_CopyBindGroupLayout = wgpuDeviceCreateBindGroupLayout(g_device, &bindGroupLayoutDescriptor);
  const WGPUPipelineLayoutDescriptor layoutDescriptor{
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &g_CopyBindGroupLayout,
  };
  auto pipelineLayout = wgpuDeviceCreatePipelineLayout(g_device, &layoutDescriptor);
  const WGPURenderPipelineDescriptor pipelineDescriptor{
      .layout = pipelineLayout,
      .vertex =
          WGPUVertexState{
              .module = module,
              .entryPoint = "vs_main",
          },
      .primitive =
          WGPUPrimitiveState{
              .topology = WGPUPrimitiveTopology_TriangleList,
          },
      .multisample =
          WGPUMultisampleState{
              .count = 1,
              .mask = UINT32_MAX,
          },
      .fragment = &fragmentState,
  };
  g_CopyPipeline = wgpuDeviceCreateRenderPipeline(g_device, &pipelineDescriptor);
  wgpuPipelineLayoutRelease(pipelineLayout);
}

void create_copy_bind_group() {
  const std::array bindGroupEntries{
      WGPUBindGroupEntry{
          .binding = 0,
          .sampler = g_graphicsConfig.msaaSamples > 1 ? g_frameBufferResolved.sampler : g_frameBuffer.sampler,
      },
      WGPUBindGroupEntry{
          .binding = 1,
          .textureView = g_graphicsConfig.msaaSamples > 1 ? g_frameBufferResolved.view : g_frameBuffer.view,
      },
  };
  const WGPUBindGroupDescriptor bindGroupDescriptor{
      .layout = g_CopyBindGroupLayout,
      .entryCount = bindGroupEntries.size(),
      .entries = bindGroupEntries.data(),
  };
  g_CopyBindGroup = wgpuDeviceCreateBindGroup(g_device, &bindGroupDescriptor);
}

static void error_callback(WGPUErrorType type, char const* message, void* userdata) {
  Log.report(LOG_FATAL, FMT_STRING("Dawn error {}: {}"), magic_enum::enum_name(static_cast<WGPUErrorType>(type)),
             message);
}

static void device_callback(WGPURequestDeviceStatus status, WGPUDevice device, char const* message, void* userdata) {
  if (status == WGPURequestDeviceStatus_Success) {
    g_device = device;
  } else {
    Log.report(LOG_WARNING, FMT_STRING("Device request failed with message: {}"), message);
  }
  *static_cast<bool*>(userdata) = true;
}

static WGPUBackendType to_wgpu_backend(AuroraBackend backend) {
  switch (backend) {
  case BACKEND_WEBGPU:
    return WGPUBackendType_WebGPU;
  case BACKEND_D3D12:
    return WGPUBackendType_D3D12;
  case BACKEND_METAL:
    return WGPUBackendType_Metal;
  case BACKEND_VULKAN:
    return WGPUBackendType_Vulkan;
  case BACKEND_OPENGL:
    return WGPUBackendType_OpenGL;
  case BACKEND_OPENGLES:
    return WGPUBackendType_OpenGLES;
  default:
    return WGPUBackendType_Null;
  }
}

bool initialize(AuroraBackend auroraBackend) {
  if (!g_Instance) {
    Log.report(LOG_INFO, FMT_STRING("Creating Dawn instance"));
    g_Instance = std::make_unique<dawn::native::Instance>();
  }
  WGPUBackendType backend = to_wgpu_backend(auroraBackend);
  Log.report(LOG_INFO, FMT_STRING("Attempting to initialize {}"), magic_enum::enum_name(backend));
#if 0
  // D3D12's debug layer is very slow
  g_Instance->EnableBackendValidation(backend != WGPUBackendType::D3D12);
#endif
  SDL_Window* window = window::get_sdl_window();
  if (!utils::DiscoverAdapter(g_Instance.get(), window, backend)) {
    return false;
  }

  {
    std::vector<dawn::native::Adapter> adapters = g_Instance->GetAdapters();
    std::sort(adapters.begin(), adapters.end(), [&](const auto& a, const auto& b) {
      WGPUAdapterProperties propertiesA;
      WGPUAdapterProperties propertiesB;
      a.GetProperties(&propertiesA);
      b.GetProperties(&propertiesB);
      constexpr std::array PreferredTypeOrder{
          WGPUAdapterType_DiscreteGPU,
          WGPUAdapterType_IntegratedGPU,
          WGPUAdapterType_CPU,
      };
      const auto typeItA = std::find(PreferredTypeOrder.begin(), PreferredTypeOrder.end(), propertiesA.adapterType);
      const auto typeItB = std::find(PreferredTypeOrder.begin(), PreferredTypeOrder.end(), propertiesB.adapterType);
      return typeItA < typeItB;
    });
    const auto adapterIt = std::find_if(adapters.begin(), adapters.end(), [=](const auto& adapter) -> bool {
      WGPUAdapterProperties properties;
      adapter.GetProperties(&properties);
      return properties.backendType == backend;
    });
    if (adapterIt == adapters.end()) {
      return false;
    }
    g_Adapter = *adapterIt;
  }
  g_Adapter.GetProperties(&g_AdapterProperties);
  g_backendType = g_AdapterProperties.backendType;
  const auto backendName = magic_enum::enum_name(g_backendType);
  Log.report(LOG_INFO, FMT_STRING("Graphics adapter information\n  API: {}\n  Device: {} ({})\n  Driver: {}"),
             backendName, g_AdapterProperties.name, magic_enum::enum_name(g_AdapterProperties.adapterType),
             g_AdapterProperties.driverDescription);

  {
    WGPUSupportedLimits supportedLimits{};
    g_Adapter.GetLimits(&supportedLimits);
    const WGPURequiredLimits requiredLimits{
        .limits =
            {
                // Use "best" supported alignments
                .minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment == 0
                                                       ? static_cast<uint32_t>(WGPU_LIMIT_U32_UNDEFINED)
                                                       : supportedLimits.limits.minUniformBufferOffsetAlignment,
                .minStorageBufferOffsetAlignment = supportedLimits.limits.minStorageBufferOffsetAlignment == 0
                                                       ? static_cast<uint32_t>(WGPU_LIMIT_U32_UNDEFINED)
                                                       : supportedLimits.limits.minStorageBufferOffsetAlignment,
            },
    };
    std::vector<WGPUFeatureName> features;
    const auto supportedFeatures = g_Adapter.GetSupportedFeatures();
    for (const auto* const feature : supportedFeatures) {
      if (strcmp(feature, "texture-compression-bc") == 0) {
        features.push_back(WGPUFeatureName_TextureCompressionBC);
      }
    }
    const std::array enableToggles {
      /* clang-format off */
#if _WIN32
      "use_dxc",
#endif
#ifdef NDEBUG
      "skip_validation",
      "disable_robustness",
#endif
      "use_user_defined_labels_in_backend",
      "disable_symbol_renaming",
      /* clang-format on */
    };
    const WGPUDawnTogglesDeviceDescriptor togglesDescriptor{
        .chain = {.sType = WGPUSType_DawnTogglesDeviceDescriptor},
        .forceEnabledTogglesCount = enableToggles.size(),
        .forceEnabledToggles = enableToggles.data(),
    };
    const WGPUDeviceDescriptor deviceDescriptor{
        .nextInChain = &togglesDescriptor.chain,
        .requiredFeaturesCount = static_cast<uint32_t>(features.size()),
        .requiredFeatures = features.data(),
        .requiredLimits = &requiredLimits,
    };
    bool deviceCallbackReceived = false;
    g_Adapter.RequestDevice(&deviceDescriptor, &device_callback, &deviceCallbackReceived);
    // while (!deviceCallbackReceived) {
    //   TODO wgpuInstanceProcessEvents
    // }
    if (!g_device) {
      return false;
    }
    wgpuDeviceSetUncapturedErrorCallback(g_device, &error_callback, nullptr);
  }
  wgpuDeviceSetDeviceLostCallback(g_device, nullptr, nullptr);
  g_queue = wgpuDeviceGetQueue(g_device);

  g_BackendBinding = std::unique_ptr<utils::BackendBinding>(utils::CreateBinding(g_backendType, window, g_device));
  if (!g_BackendBinding) {
    return false;
  }

  auto swapChainFormat = static_cast<WGPUTextureFormat>(g_BackendBinding->GetPreferredSwapChainTextureFormat());
  if (swapChainFormat == WGPUTextureFormat_RGBA8UnormSrgb) {
    swapChainFormat = WGPUTextureFormat_RGBA8Unorm;
  } else if (swapChainFormat == WGPUTextureFormat_BGRA8UnormSrgb) {
    swapChainFormat = WGPUTextureFormat_BGRA8Unorm;
  }
  Log.report(LOG_INFO, FMT_STRING("Using swapchain format {}"), magic_enum::enum_name(swapChainFormat));
  {
    const WGPUSwapChainDescriptor descriptor{
        .format = swapChainFormat,
        .implementation = g_BackendBinding->GetSwapChainImplementation(),
    };
    g_swapChain = wgpuDeviceCreateSwapChain(g_device, nullptr, &descriptor);
  }
  {
    const auto size = window::get_window_size();
    g_graphicsConfig = GraphicsConfig{
        .width = size.fb_width,
        .height = size.fb_height,
        .colorFormat = swapChainFormat,
        .depthFormat = WGPUTextureFormat_Depth32Float,
        .msaaSamples = g_config.msaa,
        .textureAnisotropy = g_config.maxTextureAnisotropy,
    };
    create_copy_pipeline();
    resize_swapchain(size.fb_width, size.fb_height, true);
    //    g_windowSize = size;
  }
  return true;
}

void shutdown() {
  wgpuBindGroupLayoutRelease(g_CopyBindGroupLayout);
  wgpuRenderPipelineRelease(g_CopyPipeline);
  wgpuBindGroupRelease(g_CopyBindGroup);
  g_frameBuffer = {};
  g_frameBufferResolved = {};
  g_depthBuffer = {};
  wgpuSwapChainRelease(g_swapChain);
  wgpuQueueRelease(g_queue);
  g_BackendBinding.reset();
  wgpuDeviceDestroy(g_device);
  g_Instance.reset();
}

void resize_swapchain(uint32_t width, uint32_t height, bool force) {
  if (!force && g_graphicsConfig.width == width && g_graphicsConfig.height == height) {
    return;
  }
  g_graphicsConfig.width = width;
  g_graphicsConfig.height = height;
  wgpuSwapChainConfigure(g_swapChain, g_graphicsConfig.colorFormat, WGPUTextureUsage_RenderAttachment, width, height);
  g_frameBuffer = create_render_texture(true);
  g_frameBufferResolved = create_render_texture(false);
  g_depthBuffer = create_depth_texture();
  create_copy_bind_group();
}
} // namespace aurora::webgpu
