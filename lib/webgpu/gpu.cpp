#include "gpu.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <aurora/aurora.h>
#include <magic_enum.hpp>
#include <webgpu/webgpu.h>
#include <webgpu/webgpu_cpp.h>

#include "../internal.hpp"
#include "../window.hpp"

#ifdef WEBGPU_DAWN
#include "../dawn/BackendBinding.hpp"
#include <dawn/native/DawnNative.h>
#endif

namespace aurora::webgpu {
static Module Log("aurora::gpu");

wgpu::Device g_device;
wgpu::Queue g_queue;
wgpu::Surface g_surface;
wgpu::BackendType g_backendType;
GraphicsConfig g_graphicsConfig;
TextureWithSampler g_frameBuffer;
TextureWithSampler g_frameBufferResolved;
TextureWithSampler g_depthBuffer;

// EFB -> XFB copy pipeline
static wgpu::BindGroupLayout g_CopyBindGroupLayout;
wgpu::RenderPipeline g_CopyPipeline;
wgpu::BindGroup g_CopyBindGroup;

static wgpu::Adapter g_adapter;
wgpu::Instance g_instance;
static wgpu::AdapterInfo g_adapterInfo;

TextureWithSampler create_render_texture(bool multisampled) {
  const wgpu::Extent3D size{
      .width = g_graphicsConfig.surfaceConfiguration.width,
      .height = g_graphicsConfig.surfaceConfiguration.height,
      .depthOrArrayLayers = 1,
  };
  const auto format = g_graphicsConfig.surfaceConfiguration.format;
  uint32_t sampleCount = 1;
  if (multisampled) {
    sampleCount = g_graphicsConfig.msaaSamples;
  }
  const wgpu::TextureDescriptor textureDescriptor{
      .label = "Render texture",
      .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc |
               wgpu::TextureUsage::CopyDst,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = format,
      .mipLevelCount = 1,
      .sampleCount = sampleCount,
  };
  auto texture = g_device.CreateTexture(&textureDescriptor);

  const wgpu::TextureViewDescriptor viewDescriptor{
      .label = "Render texture view",
      .dimension = wgpu::TextureViewDimension::e2D,
      .mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED,
      .arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED,
  };
  auto view = texture.CreateView(&viewDescriptor);

  const wgpu::SamplerDescriptor samplerDescriptor{
      .label = "Render sampler",
      .addressModeU = wgpu::AddressMode::ClampToEdge,
      .addressModeV = wgpu::AddressMode::ClampToEdge,
      .addressModeW = wgpu::AddressMode::ClampToEdge,
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
      .mipmapFilter = wgpu::MipmapFilterMode::Linear,
      .lodMinClamp = 0.f,
      .lodMaxClamp = 1000.f,
      .maxAnisotropy = 1,
  };
  auto sampler = g_device.CreateSampler(&samplerDescriptor);

  return {
      .texture = std::move(texture),
      .view = std::move(view),
      .size = size,
      .format = format,
      .sampler = std::move(sampler),
  };
}

static TextureWithSampler create_depth_texture() {
  const wgpu::Extent3D size{
      .width = g_graphicsConfig.surfaceConfiguration.width,
      .height = g_graphicsConfig.surfaceConfiguration.height,
      .depthOrArrayLayers = 1,
  };
  const auto format = g_graphicsConfig.depthFormat;
  const wgpu::TextureDescriptor textureDescriptor{
      .label = "Depth texture",
      .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding,
      .dimension = wgpu::TextureDimension::e2D,
      .size = size,
      .format = format,
      .mipLevelCount = 1,
      .sampleCount = g_graphicsConfig.msaaSamples,
  };
  auto texture = g_device.CreateTexture(&textureDescriptor);

  const wgpu::TextureViewDescriptor viewDescriptor{
      .label = "Depth texture view",
      .dimension = wgpu::TextureViewDimension::e2D,
      .mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED,
      .arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED,
  };
  auto view = texture.CreateView(&viewDescriptor);

  const wgpu::SamplerDescriptor samplerDescriptor{
      .label = "Depth sampler",
      .addressModeU = wgpu::AddressMode::ClampToEdge,
      .addressModeV = wgpu::AddressMode::ClampToEdge,
      .addressModeW = wgpu::AddressMode::ClampToEdge,
      .magFilter = wgpu::FilterMode::Linear,
      .minFilter = wgpu::FilterMode::Linear,
      .mipmapFilter = wgpu::MipmapFilterMode::Linear,
      .lodMinClamp = 0.f,
      .lodMaxClamp = 1000.f,
      .maxAnisotropy = 1,
  };
  auto sampler = g_device.CreateSampler(&samplerDescriptor);

  return {
      .texture = std::move(texture),
      .view = std::move(view),
      .size = size,
      .format = format,
      .sampler = std::move(sampler),
  };
}

void create_copy_pipeline() {
  wgpu::ShaderSourceWGSL sourceDescriptor{};
  sourceDescriptor.code = R"""(
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

@vertex
fn vs_main(@builtin(vertex_index) vtxIdx: u32) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4<f32>(pos[vtxIdx], 0.0, 1.0);
    out.uv = uvs[vtxIdx];
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    return textureSample(efb_texture, efb_sampler, in.uv);
}
)""";
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &sourceDescriptor,
      .label = "XFB Copy Module",
  };
  auto module = g_device.CreateShaderModule(&moduleDescriptor);
  const std::array colorTargets{wgpu::ColorTargetState{
      .format = g_graphicsConfig.surfaceConfiguration.format,
      .writeMask = wgpu::ColorWriteMask::All,
  }};
  const wgpu::FragmentState fragmentState{
      .module = module,
      .entryPoint = "fs_main",
      .targetCount = colorTargets.size(),
      .targets = colorTargets.data(),
  };
  const std::array bindGroupLayoutEntries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Fragment,
          .sampler =
              wgpu::SamplerBindingLayout{
                  .type = wgpu::SamplerBindingType::Filtering,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 1,
          .visibility = wgpu::ShaderStage::Fragment,
          .texture =
              wgpu::TextureBindingLayout{
                  .sampleType = wgpu::TextureSampleType::Float,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      },
  };
  const wgpu::BindGroupLayoutDescriptor bindGroupLayoutDescriptor{
      .entryCount = bindGroupLayoutEntries.size(),
      .entries = bindGroupLayoutEntries.data(),
  };
  g_CopyBindGroupLayout = g_device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);
  const wgpu::PipelineLayoutDescriptor layoutDescriptor{
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &g_CopyBindGroupLayout,
  };
  auto pipelineLayout = g_device.CreatePipelineLayout(&layoutDescriptor);
  const wgpu::RenderPipelineDescriptor pipelineDescriptor{
      .layout = pipelineLayout,
      .vertex =
          wgpu::VertexState{
              .module = module,
              .entryPoint = "vs_main",
          },
      .primitive =
          wgpu::PrimitiveState{
              .topology = wgpu::PrimitiveTopology::TriangleList,
          },
      .multisample =
          wgpu::MultisampleState{
              .count = 1,
              .mask = UINT32_MAX,
          },
      .fragment = &fragmentState,
  };
  g_CopyPipeline = g_device.CreateRenderPipeline(&pipelineDescriptor);
}

void create_copy_bind_group() {
  const std::array bindGroupEntries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .sampler = g_graphicsConfig.msaaSamples > 1 ? g_frameBufferResolved.sampler : g_frameBuffer.sampler,
      },
      wgpu::BindGroupEntry{
          .binding = 1,
          .textureView = g_graphicsConfig.msaaSamples > 1 ? g_frameBufferResolved.view : g_frameBuffer.view,
      },
  };
  const wgpu::BindGroupDescriptor bindGroupDescriptor{
      .layout = g_CopyBindGroupLayout,
      .entryCount = bindGroupEntries.size(),
      .entries = bindGroupEntries.data(),
  };
  g_CopyBindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
}

static wgpu::BackendType to_wgpu_backend(AuroraBackend backend) {
  switch (backend) {
  case BACKEND_WEBGPU:
    return wgpu::BackendType::WebGPU;
  case BACKEND_D3D11:
    return wgpu::BackendType::D3D11;
  case BACKEND_D3D12:
    return wgpu::BackendType::D3D12;
  case BACKEND_METAL:
    return wgpu::BackendType::Metal;
  case BACKEND_VULKAN:
    return wgpu::BackendType::Vulkan;
  case BACKEND_OPENGL:
    return wgpu::BackendType::OpenGL;
  case BACKEND_OPENGLES:
    return wgpu::BackendType::OpenGLES;
  default:
    return wgpu::BackendType::Null;
  }
}

bool initialize(AuroraBackend auroraBackend) {
  if (!g_instance) {
#ifdef WEBGPU_DAWN
    Log.info("Initializing Dawn");
#endif
    Log.info("Creating WGPU instance");
    const std::array requiredInstanceFeatures{
        wgpu::InstanceFeatureName::TimedWaitAny,
    };
    wgpu::InstanceDescriptor instanceDescriptor{
        .requiredFeatureCount = requiredInstanceFeatures.size(),
        .requiredFeatures = requiredInstanceFeatures.data(),
    };
#ifdef WEBGPU_DAWN
    dawn::native::DawnInstanceDescriptor dawnInstanceDescriptor;
    dawnInstanceDescriptor.backendValidationLevel = dawn::native::BackendValidationLevel::Disabled;
    instanceDescriptor.nextInChain = &dawnInstanceDescriptor;
#endif
    g_instance = wgpu::CreateInstance(&instanceDescriptor);
    if (!g_instance) {
      Log.error("Failed to create WGPU instance");
      return false;
    }
  }
  const wgpu::BackendType backend = to_wgpu_backend(auroraBackend);
#ifdef EMSCRIPTEN
  if (backend != wgpu::BackendType::WebGPU) {
    Log.warn("Backend type {} unsupported", magic_enum::enum_name(backend));
    return false;
  }
#endif
  Log.info("Attempting to initialize {}", magic_enum::enum_name(backend));
#if 0
  // D3D12's debug layer is very slow
  g_dawnInstance->EnableBackendValidation(backend != WGPUBackendType::D3D12);
#endif

#ifdef EMSCRIPTEN
  const WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDescriptor{
      .chain = {.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector},
      .selector = "#canvas",
  };
#else
  SDL_Window* window = window::get_sdl_window();
  const auto chainedDescriptor = utils::SetupWindowAndGetSurfaceDescriptor(window);
#endif
  const wgpu::SurfaceDescriptor surfaceDescriptor{
      .nextInChain = chainedDescriptor.get(),
      .label = "Surface",
  };
  g_surface = g_instance.CreateSurface(&surfaceDescriptor);
  if (!g_surface) {
    Log.error("Failed to create surface");
    return false;
  }
  {
    const wgpu::RequestAdapterOptions options{
        .powerPreference = wgpu::PowerPreference::HighPerformance,
        .backendType = backend,
        .compatibleSurface = g_surface,
    };
    const auto future = g_instance.RequestAdapter(
        &options, wgpu::CallbackMode::WaitAnyOnly,
        [](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
          if (status == wgpu::RequestAdapterStatus::Success) {
            g_adapter = std::move(adapter);
          } else {
            Log.warn("Adapter request failed: {}", message);
          }
        });
    const auto status = g_instance.WaitAny(future, 5000000000);
    if (status != wgpu::WaitStatus::Success) {
      Log.error("Failed to create adapter: {}", magic_enum::enum_name(status));
      return false;
    }
    if (!g_adapter) {
      Log.error("Failed to create adapter");
      return false;
    }
  }
  g_adapter.GetInfo(&g_adapterInfo);
  g_backendType = g_adapterInfo.backendType;
  const auto backendName = magic_enum::enum_name(g_backendType);
  auto adapterName = g_adapterInfo.device;
  if (adapterName.IsUndefined()) {
    adapterName = wgpu::StringView("Unknown");
  }
  auto description = g_adapterInfo.description;
  if (description.IsUndefined()) {
    description = wgpu::StringView("Unknown");
  }
  Log.info("Graphics adapter information\n  API: {}\n  Device: {} ({})\n  Driver: {}", backendName, adapterName,
           magic_enum::enum_name(g_adapterInfo.adapterType), description);

  {
    wgpu::Limits supportedLimits{};
    g_adapter.GetLimits(&supportedLimits);
    const wgpu::Limits requiredLimits{
        // Use "best" supported limits
        .maxTextureDimension1D = supportedLimits.maxTextureDimension1D == 0 ? WGPU_LIMIT_U32_UNDEFINED
                                                                            : supportedLimits.maxTextureDimension1D,
        .maxTextureDimension2D = supportedLimits.maxTextureDimension2D == 0 ? WGPU_LIMIT_U32_UNDEFINED
                                                                            : supportedLimits.maxTextureDimension2D,
        .maxTextureDimension3D = supportedLimits.maxTextureDimension3D == 0 ? WGPU_LIMIT_U32_UNDEFINED
                                                                            : supportedLimits.maxTextureDimension3D,
        .maxTextureArrayLayers = supportedLimits.maxTextureArrayLayers == 0 ? WGPU_LIMIT_U32_UNDEFINED
                                                                            : supportedLimits.maxTextureArrayLayers,
        .maxDynamicStorageBuffersPerPipelineLayout = supportedLimits.maxDynamicStorageBuffersPerPipelineLayout == 0
                                                         ? WGPU_LIMIT_U32_UNDEFINED
                                                         : supportedLimits.maxDynamicStorageBuffersPerPipelineLayout,
        .maxStorageBuffersPerShaderStage = supportedLimits.maxStorageBuffersPerShaderStage == 0
                                               ? WGPU_LIMIT_U32_UNDEFINED
                                               : supportedLimits.maxStorageBuffersPerShaderStage,
        .minUniformBufferOffsetAlignment = supportedLimits.minUniformBufferOffsetAlignment < 64
                                               ? 64
                                               : supportedLimits.minUniformBufferOffsetAlignment,
        .minStorageBufferOffsetAlignment = supportedLimits.minStorageBufferOffsetAlignment < 16
                                               ? 16
                                               : supportedLimits.minStorageBufferOffsetAlignment,
    };
    Log.info(
        "Using limits:"
        "\n  maxTextureDimension1D: {}"
        "\n  maxTextureDimension2D: {}"
        "\n  maxTextureDimension3D: {}"
        "\n  maxTextureArrayLayers: {}"
        "\n  maxDynamicStorageBuffersPerPipelineLayout: {}"
        "\n  maxStorageBuffersPerShaderStage: {}"
        "\n  minUniformBufferOffsetAlignment: {}"
        "\n  minStorageBufferOffsetAlignment: {}",
        requiredLimits.maxTextureDimension1D, requiredLimits.maxTextureDimension2D,
        requiredLimits.maxTextureDimension3D, requiredLimits.maxTextureArrayLayers,
        requiredLimits.maxDynamicStorageBuffersPerPipelineLayout, requiredLimits.maxStorageBuffersPerShaderStage,
        requiredLimits.minUniformBufferOffsetAlignment, requiredLimits.minStorageBufferOffsetAlignment);
    std::vector<wgpu::FeatureName> requiredFeatures;
    wgpu::SupportedFeatures supportedFeatures;
    g_adapter.GetFeatures(&supportedFeatures);
    for (size_t i = 0; i < supportedFeatures.featureCount; ++i) {
      const auto feature = supportedFeatures.features[i];
      if (feature == wgpu::FeatureName::TextureCompressionBC) {
        requiredFeatures.push_back(feature);
      }
    }
#ifdef WEBGPU_DAWN
    const std::array enableToggles{
    /* clang-format off */
#if _WIN32
      "use_dxc",
#ifdef NDEBUG
      "emit_hlsl_debug_symbols",
#endif
#endif
#ifdef NDEBUG
      "skip_validation",
      "disable_robustness",
#endif
      "use_user_defined_labels_in_backend",
      "disable_symbol_renaming",
      "enable_immediate_error_handling",
        /* clang-format on */
    };
    const wgpu::DawnTogglesDescriptor togglesDescriptor({
        .enabledToggleCount = enableToggles.size(),
        .enabledToggles = enableToggles.data(),
    });
#endif
    wgpu::DeviceDescriptor deviceDescriptor({
#ifdef WEBGPU_DAWN
        .nextInChain = &togglesDescriptor,
#endif
        .requiredFeatureCount = requiredFeatures.size(),
        .requiredFeatures = requiredFeatures.data(),
        .requiredLimits = &requiredLimits,
    });
    deviceDescriptor.SetUncapturedErrorCallback(
        [](const wgpu::Device& device, wgpu::ErrorType type, wgpu::StringView message) {
          FATAL("WebGPU error {}: {}", underlying(type), message);
        });
    deviceDescriptor.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
                                           [](const wgpu::Device& device, wgpu::DeviceLostReason reason,
                                              wgpu::StringView message) { Log.warn("Device lost: {}", message); });
    const auto future =
        g_adapter.RequestDevice(&deviceDescriptor, wgpu::CallbackMode::WaitAnyOnly,
                                [](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
                                  if (status == wgpu::RequestDeviceStatus::Success) {
                                    g_device = std::move(device);
                                  } else {
                                    Log.warn("Device request failed: {}", message);
                                  }
                                });
    const auto status = g_instance.WaitAny(future, 5000000000);
    if (status != wgpu::WaitStatus::Success) {
      Log.error("Failed to create device: {}", magic_enum::enum_name(status));
      return false;
    }
    if (!g_device) {
      return false;
    }
    g_device.SetLoggingCallback([](wgpu::LoggingType type, wgpu::StringView message) {
      AuroraLogLevel level = LOG_FATAL;
      switch (type) {
      case wgpu::LoggingType::Verbose:
        level = LOG_DEBUG;
        break;
      case wgpu::LoggingType::Info:
        level = LOG_INFO;
        break;
      case wgpu::LoggingType::Warning:
        level = LOG_WARNING;
        break;
      case wgpu::LoggingType::Error:
        level = LOG_ERROR;
        break;
      default:
        break;
      }
      Log.report(level, "WebGPU message: {}", message);
    });
  }
  g_queue = g_device.GetQueue();

  wgpu::SurfaceCapabilities surfaceCapabilities;
  const wgpu::Status status = g_surface.GetCapabilities(g_adapter, &surfaceCapabilities);
  if (status != wgpu::Status::Success) {
    Log.error("Failed to get surface capabilities: {}", magic_enum::enum_name(status));
    return false;
  }
  if (surfaceCapabilities.formatCount == 0) {
    Log.error("Surface has no formats");
    return false;
  }
  if (surfaceCapabilities.presentModeCount == 0) {
    Log.error("Surface has no present modes");
    return false;
  }
  auto surfaceFormat = surfaceCapabilities.formats[0];
  auto presentMode = surfaceCapabilities.presentModes[0];
  if (surfaceFormat == wgpu::TextureFormat::RGBA8UnormSrgb) {
    surfaceFormat = wgpu::TextureFormat::RGBA8Unorm;
  } else if (surfaceFormat == wgpu::TextureFormat::BGRA8UnormSrgb) {
    surfaceFormat = wgpu::TextureFormat::BGRA8Unorm;
  }
  Log.info("Using surface format {}, present mode {}", magic_enum::enum_name(surfaceFormat),
           magic_enum::enum_name(presentMode));
  const auto size = window::get_window_size();
  g_graphicsConfig = GraphicsConfig{
      .surfaceConfiguration =
          wgpu::SurfaceConfiguration{
              .format = surfaceFormat,
              .usage = wgpu::TextureUsage::RenderAttachment,
              .width = size.fb_width,
              .height = size.fb_height,
              .presentMode = presentMode,
          },
      .depthFormat = wgpu::TextureFormat::Depth32Float,
      .msaaSamples = g_config.msaa,
      .textureAnisotropy = g_config.maxTextureAnisotropy,
  };
  create_copy_pipeline();
  resize_swapchain(size.fb_width, size.fb_height, true);
  return true;
}

void shutdown() {
  g_CopyBindGroupLayout = {};
  g_CopyPipeline = {};
  g_CopyBindGroup = {};
  g_frameBuffer = {};
  g_frameBufferResolved = {};
  g_depthBuffer = {};
  g_queue = {};
  g_surface = {};
  g_device = {};
  g_adapter = {};
  g_instance = {};
}

void resize_swapchain(uint32_t width, uint32_t height, bool force) {
  if (!force && g_graphicsConfig.surfaceConfiguration.width == width &&
      g_graphicsConfig.surfaceConfiguration.height == height) {
    return;
  }
  g_graphicsConfig.surfaceConfiguration.width = width;
  g_graphicsConfig.surfaceConfiguration.height = height;
  auto surfaceConfiguration = g_graphicsConfig.surfaceConfiguration;
  surfaceConfiguration.device = g_device;
  g_surface.Configure(&surfaceConfiguration);
  g_frameBuffer = create_render_texture(true);
  g_frameBufferResolved = create_render_texture(false);
  g_depthBuffer = create_depth_texture();
  create_copy_bind_group();
}
} // namespace aurora::webgpu
