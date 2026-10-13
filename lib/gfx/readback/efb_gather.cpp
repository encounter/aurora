#include "gpu.hpp"

#include "../efb_wgsl.hpp"
#include "../resources.hpp"
#include "../../webgpu/gpu.hpp"
#include "../../webgpu/gpu_prof.hpp"

#include <array>
#include <string>

namespace aurora::gfx::readback::gpu {
namespace {
using webgpu::g_device;

constexpr uint32_t WorkgroupSize = 64;

constexpr std::string_view ShaderBody = R"(
struct Job {
    x: u32,
    y: u32,
    width: u32,
    height: u32,
    outWord: u32,
    color: u32,
    alphaMode: u32,
    pixelFormat: u32,
};

struct Params {
    jobCount: u32,
    totalWords: u32,
    _pad0: u32,
    _pad1: u32,
    scaleNumX: u32,
    scaleDenX: u32,
    scaleNumY: u32,
    scaleDenY: u32,
    targetWidth: u32,
    targetHeight: u32,
    _pad2: u32,
    _pad3: u32,
};

@group(0) @binding(0) var depthTex: texture_depth_2d;
@group(0) @binding(1) var colorTex: texture_2d<f32>;
@group(0) @binding(2) var<storage, read> jobs: array<Job>;
@group(0) @binding(3) var<uniform> params: Params;
@group(0) @binding(4) var<storage, read_write> outWords: array<u32>;

// Target pixel in the center of the sample
fn target_coord(v: u32, num: u32, den: u32, size: u32) -> i32 {
    return i32(min(((2u * v + 1u) * num) / (2u * den), size - 1u));
}

@compute @workgroup_size(64, 1, 1)
fn cs_main(@builtin(global_invocation_id) id: vec3u, @builtin(num_workgroups) groups: vec3u) {
    let word = id.y * groups.x * 64u + id.x;
    if (word >= params.totalWords) {
        return;
    }
    var lo = 0u;
    var hi = params.jobCount;
    loop {
        if (hi - lo <= 1u) {
            break;
        }
        let mid = (lo + hi) / 2u;
        if (jobs[mid].outWord <= word) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    let job = jobs[lo];
    let local = word - job.outWord;
    let coord = vec2i(target_coord(job.x + local % job.width, params.scaleNumX, params.scaleDenX, params.targetWidth),
                      target_coord(job.y + local / job.width, params.scaleNumY, params.scaleDenY, params.targetHeight));
    var value = 0u;
    if (job.color == 0u) {
        value = gx_z24_from_depth(textureLoad(depthTex, coord, 0));
    } else {
        let c = efb_quantize(unorm8(textureLoad(colorTex, coord, 0)), job.pixelFormat);
        var a = c.a;
        if (job.alphaMode == 0u) { // GX_READ_00
            a = 0u;
        } else if (job.alphaMode == 1u) { // GX_READ_FF
            a = 255u;
        }
        value = (a << 24u) | (c.r << 16u) | (c.g << 8u) | c.b;
    }
    outWords[word] = value;
}
)";

wgpu::BindGroupLayout g_bindGroupLayout;
wgpu::ComputePipeline g_pipeline;

std::string shader_source() {
  std::string source;
  source += efb_wgsl::z24();
  source += efb_wgsl::Color;
  source += ShaderBody;
  return source;
}
} // namespace

void initialize_efb_gather() {
  constexpr std::array entries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Compute,
          .texture =
              wgpu::TextureBindingLayout{
                  .sampleType = wgpu::TextureSampleType::Depth,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 1,
          .visibility = wgpu::ShaderStage::Compute,
          .texture =
              wgpu::TextureBindingLayout{
                  .sampleType = wgpu::TextureSampleType::UnfilterableFloat,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 2,
          .visibility = wgpu::ShaderStage::Compute,
          .buffer = wgpu::BufferBindingLayout{.type = wgpu::BufferBindingType::ReadOnlyStorage},
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 3,
          .visibility = wgpu::ShaderStage::Compute,
          .buffer = wgpu::BufferBindingLayout{.type = wgpu::BufferBindingType::Uniform},
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 4,
          .visibility = wgpu::ShaderStage::Compute,
          .buffer = wgpu::BufferBindingLayout{.type = wgpu::BufferBindingType::Storage},
      },
  };
  const wgpu::BindGroupLayoutDescriptor layoutDescriptor{
      .label = "Readback EFB Gather Bind Group Layout",
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  g_bindGroupLayout = g_device.CreateBindGroupLayout(&layoutDescriptor);

  const auto source = shader_source();
  const wgpu::ShaderSourceWGSL wgslSource{wgpu::ShaderSourceWGSL::Init{.code = source.c_str()}};
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &wgslSource,
      .label = "Readback EFB Gather",
  };
  const auto module = g_device.CreateShaderModule(&moduleDescriptor);
  constexpr wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor{
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &g_bindGroupLayout,
  };
  const auto pipelineLayout = g_device.CreatePipelineLayout(&pipelineLayoutDescriptor);
  const wgpu::ComputePipelineDescriptor pipelineDescriptor{
      .label = "Readback EFB Gather",
      .layout = pipelineLayout,
      .compute = wgpu::ComputeState{.module = module, .entryPoint = "cs_main"},
  };
  g_pipeline = g_device.CreateComputePipeline(&pipelineDescriptor);
}

void shutdown_efb_gather() {
  g_pipeline = {};
  g_bindGroupLayout = {};
}

void encode_efb_gather(const wgpu::CommandEncoder& cmd, const Job& job, const wgpu::Buffer& out) {
  const auto& res = detail::resources();
  const std::array entries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .textureView = job.depthView,
      },
      wgpu::BindGroupEntry{
          .binding = 1,
          .textureView = job.colorView,
      },
      wgpu::BindGroupEntry{
          .binding = 2,
          .buffer = res.storageBuffer,
          .offset = job.gatherJobs.offset,
          .size = job.gatherJobs.size,
      },
      wgpu::BindGroupEntry{
          .binding = 3,
          .buffer = res.uniformBuffer,
          .offset = job.gatherParams.offset,
          .size = sizeof(EfbGatherParams),
      },
      wgpu::BindGroupEntry{.binding = 4, .buffer = out, .size = job.outputBytes},
  };
  const wgpu::BindGroupDescriptor bindGroupDescriptor{
      .label = "Readback EFB Gather Bind Group",
      .layout = g_bindGroupLayout,
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  const auto bindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  const wgpu::ComputePassDescriptor passDescriptor{
      .label = "Readback EFB Gather",
      .timestampWrites = webgpu::gpu_prof::pass_writes("Readback gather"),
  };
  const auto pass = cmd.BeginComputePass(&passDescriptor);
  pass.SetPipeline(g_pipeline);
  pass.SetBindGroup(0, bindGroup);
  dispatch_linear(pass, (job.gatherWords + WorkgroupSize - 1) / WorkgroupSize);
  pass.End();
}

} // namespace aurora::gfx::readback::gpu
