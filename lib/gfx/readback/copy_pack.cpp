#include "gpu.hpp"

#include "../efb_wgsl.hpp"
#include "../resources.hpp"
#include "../texture.hpp"
#include "../../webgpu/gpu.hpp"
#include "../../webgpu/gpu_prof.hpp"

#include <array>
#include <string>

namespace aurora::gfx::readback::gpu {
namespace {
using webgpu::g_device;

constexpr uint32_t WorkgroupSize = 64;

constexpr std::string_view ShaderBody = R"(
struct Params {
    family: u32,
    encoding: u32,
    tileWidth: u32,
    tileHeight: u32,
    tileBytes: u32,
    tilesX: u32,
    width: u32,
    height: u32,
    srcWidth: u32,
    srcHeight: u32,
    wordCount: u32,
    outWord: u32,
    quantizeRgba6: u32,
    _pad0: u32,
    _pad1: u32,
    _pad2: u32,
};

@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var<uniform> params: Params;
@group(0) @binding(2) var<storage, read_write> outWords: array<u32>;

fn source_coord(x: u32, logical: u32, size: u32) -> i32 {
    return i32(min(((2u * x + 1u) * size) / (2u * logical), size - 1u));
}

fn load(x: u32, y: u32) -> vec4u {
    if (x >= params.width || y >= params.height) {
        return vec4u(0u);
    }
    let c = unorm8(textureLoad(src, vec2i(source_coord(x, params.width, params.srcWidth),
                                          source_coord(y, params.height, params.srcHeight)), 0));
    if (params.quantizeRgba6 != 0u) {
        return efb_quantize_rgba6(c);
    }
    return c;
}

fn nibble(v: u32) -> u32 {
    return (v * 15u + 127u) / 255u;
}

fn encode_byte(offset: u32) -> u32 {
    let tile = offset / params.tileBytes;
    let within = offset % params.tileBytes;
    let baseX = (tile % params.tilesX) * params.tileWidth;
    let baseY = (tile / params.tilesX) * params.tileHeight;
    var x = 0u;
    var y = 0u;
    var component = 0u;
    switch params.family {
        case 0u: { // 4 bpp
            x = baseX + (within % 4u) * 2u;
            y = baseY + within / 4u;
        }
        case 1u: { // 8 bpp
            x = baseX + within % 8u;
            y = baseY + within / 8u;
        }
        case 2u: { // 16 bpp
            let t = within / 2u;
            x = baseX + t % 4u;
            y = baseY + t / 4u;
            component = within % 2u;
        }
        default: { // 32 bpp: AR block, then GB block
            let t = (within % 32u) / 2u;
            x = baseX + t % 4u;
            y = baseY + t / 4u;
            component = (within / 32u) * 2u + within % 2u;
        }
    }
    let c = load(x, y);
    switch params.encoding {
        case 0u: { // Red
            return c.r;
        }
        case 1u: { // Nibble
            return (nibble(c.r) << 4u) | nibble(load(x + 1u, y).r);
        }
        case 2u: { // NibblePairAR
            return (nibble(c.a) << 4u) | nibble(c.r);
        }
        case 3u: { // PairAR
            return select(c.r, c.a, component == 0u);
        }
        case 4u: { // RGB565
            let v = ((c.r >> 3u) << 11u) | ((c.g >> 2u) << 5u) | (c.b >> 3u);
            return select(v & 0xffu, v >> 8u, component == 0u);
        }
        case 5u: { // RGB5A3
            var v = ((c.a >> 5u) << 12u) | ((c.r >> 4u) << 8u) | ((c.g >> 4u) << 4u) | (c.b >> 4u);
            if (c.a >= 224u) {
                v = 0x8000u | ((c.r >> 3u) << 10u) | ((c.g >> 3u) << 5u) | (c.b >> 3u);
            }
            return select(v & 0xffu, v >> 8u, component == 0u);
        }
        default: { // RGBA8
            var channels = array<u32, 4>(c.a, c.r, c.g, c.b);
            return channels[component];
        }
    }
}

@compute @workgroup_size(64, 1, 1)
fn cs_main(@builtin(global_invocation_id) id: vec3u, @builtin(num_workgroups) groups: vec3u) {
    let word = id.y * groups.x * 64u + id.x;
    if (word >= params.wordCount) {
        return;
    }
    let base = word * 4u;
    outWords[params.outWord + word] = encode_byte(base) | (encode_byte(base + 1u) << 8u) |
                                      (encode_byte(base + 2u) << 16u) | (encode_byte(base + 3u) << 24u);
}
)";

wgpu::BindGroupLayout g_bindGroupLayout;
wgpu::ComputePipeline g_pipeline;
} // namespace

void initialize_copy_pack() {
  constexpr std::array entries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Compute,
          .texture =
              wgpu::TextureBindingLayout{
                  .sampleType = wgpu::TextureSampleType::UnfilterableFloat,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 1,
          .visibility = wgpu::ShaderStage::Compute,
          .buffer = wgpu::BufferBindingLayout{.type = wgpu::BufferBindingType::Uniform},
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 2,
          .visibility = wgpu::ShaderStage::Compute,
          .buffer = wgpu::BufferBindingLayout{.type = wgpu::BufferBindingType::Storage},
      },
  };
  const wgpu::BindGroupLayoutDescriptor layoutDescriptor{
      .label = "Readback Copy Pack Bind Group Layout",
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  g_bindGroupLayout = g_device.CreateBindGroupLayout(&layoutDescriptor);

  std::string source;
  source += efb_wgsl::Color;
  source += ShaderBody;
  const wgpu::ShaderSourceWGSL wgslSource{wgpu::ShaderSourceWGSL::Init{.code = source.c_str()}};
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &wgslSource,
      .label = "Readback Copy Pack",
  };
  const auto module = g_device.CreateShaderModule(&moduleDescriptor);
  constexpr wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor{
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &g_bindGroupLayout,
  };
  const auto pipelineLayout = g_device.CreatePipelineLayout(&pipelineLayoutDescriptor);
  const wgpu::ComputePipelineDescriptor pipelineDescriptor{
      .label = "Readback Copy Pack",
      .layout = pipelineLayout,
      .compute = wgpu::ComputeState{.module = module, .entryPoint = "cs_main"},
  };
  g_pipeline = g_device.CreateComputePipeline(&pipelineDescriptor);
}

void shutdown_copy_pack() {
  g_pipeline = {};
  g_bindGroupLayout = {};
}

void encode_copy_pack(const wgpu::CommandEncoder& cmd, const Job& job, const wgpu::Buffer& out) {
  const auto& res = detail::resources();
  const wgpu::ComputePassDescriptor passDescriptor{
      .label = "Readback Copy Pack",
      .timestampWrites = webgpu::gpu_prof::pass_writes("Readback pack"),
  };
  const auto pass = cmd.BeginComputePass(&passDescriptor);
  pass.SetPipeline(g_pipeline);
  for (const auto& source : job.copies) {
    const std::array entries{
        wgpu::BindGroupEntry{
            .binding = 0,
            .textureView = source.texture->sampleTextureView,
        },
        wgpu::BindGroupEntry{
            .binding = 1,
            .buffer = res.uniformBuffer,
            .offset = source.params.offset,
            .size = sizeof(CopyPackParams),
        },
        wgpu::BindGroupEntry{
            .binding = 2,
            .buffer = out,
            .size = job.outputBytes,
        },
    };
    const wgpu::BindGroupDescriptor bindGroupDescriptor{
        .label = "Readback Copy Pack Bind Group",
        .layout = g_bindGroupLayout,
        .entryCount = entries.size(),
        .entries = entries.data(),
    };
    pass.SetBindGroup(0, g_device.CreateBindGroup(&bindGroupDescriptor));
    dispatch_linear(pass, (source.wordCount + WorkgroupSize - 1) / WorkgroupSize);
  }
  pass.End();
}

} // namespace aurora::gfx::readback::gpu
