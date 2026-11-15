// NativeXRenderer - see dof.h.

#include "dof.h"

#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include "glsl.h"

namespace native_x_renderer {

using namespace Diligent;

namespace {

std::atomic<float> g_ads_blend{0.0f};
std::atomic<int64_t> g_ads_until{0};  // steady clock ticks

constexpr const char* kVS = R"(#version 450
void main() {
  vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char* kPS = R"(#version 450
layout(std140, set = 0, binding = 0) uniform DofConstants {
  vec4 size;   // the view in the colour: width, height; in the depth: width, height
  vec4 lens;   // the focus (metres: no blur from there on), the nearest (the most blur), the largest radius in pixels, the taps
};
layout(set = 0, binding = 1) uniform sampler2D colorTex;
layout(set = 0, binding = 2) uniform sampler2D depthTex;
layout(location = 0) out vec4 out_color;

// A pixel's blur radius in colour pixels, from its distance (the stored depth is 0.1 / metres).
float Radius(vec2 at) {
  const ivec2 q = ivec2(clamp(at, vec2(0.0), size.xy - 1.0) * (size.zw / size.xy));
  const float metres = 0.1 / max(texelFetch(depthTex, q, 0).r, 1e-6);
  return clamp((lens.x - metres) / (lens.x - lens.y), 0.0, 1.0) * lens.z;
}

void main() {
  const vec2 p = gl_FragCoord.xy;
  const vec4 center = texelFetch(colorTex, ivec2(p), 0);
  out_color = center;
  if (any(greaterThanEqual(p, size.xy)) || lens.z < 0.5) return;
  const float own = Radius(p);
  const int taps = int(lens.w);
  // Interleaved gradient noise: each pixel's spiral is turned differently.
  const float noise = fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
  vec3 sum = vec3(0.0);
  float weight = 0.0, covered = 0.0;
  for (int i = 0; i < taps; ++i) {
    const float r = lens.z * sqrt((float(i) + 0.5) / float(taps));
    const float angle = float(i) * 2.39996323 + noise * 6.2831853;
    const vec2 at = p + r * vec2(cos(angle), sin(angle));
    if (any(lessThan(at, vec2(0.0))) || any(greaterThanEqual(at, size.xy))) continue;
    const float theirs = Radius(at);
    // The tap's own blur reaches this pixel (a soft thing spreads over what is beside it); or this pixel is soft
    // and the tap is soft too (inside the soft thing: the sharp world behind its edge does not leak in).
    const float reach = clamp(theirs - r + 1.0, 0.0, 1.0);
    const float inside = clamp(own - r + 1.0, 0.0, 1.0) * clamp(theirs, 0.0, 1.0);
    const float w = max(reach, inside);
    sum += max(textureLod(colorTex, at / vec2(textureSize(colorTex, 0)), 0.0).rgb, vec3(0.0)) * w;
    weight += w;
    covered += reach;
  }
  if (weight < 1e-3) return;
  // How much of the gathered colour shows: all of it in a soft pixel, by the share of taps that reach a sharp one.
  const float show = max(clamp(own, 0.0, 1.0), clamp(covered / float(taps) * 2.0, 0.0, 1.0));
  out_color = vec4(mix(max(center.rgb, vec3(0.0)), sum / weight, show), center.a);
}
)";

RefCntAutoPtr<IShader> Shader(IRenderDevice* device, SHADER_TYPE type, const char* source, const char* name) {
  RefCntAutoPtr<IShader> shader;
  std::vector<uint32_t> spirv =
      CompileGlsl(type == SHADER_TYPE_VERTEX ? GlslStage::kVertex : GlslStage::kPixel, source, name);
  if (spirv.empty()) return shader;
  ShaderCreateInfo info;
  info.Desc.Name = name;
  info.Desc.ShaderType = type;
  info.Desc.UseCombinedTextureSamplers = true;
  info.ByteCode = spirv.data();
  info.ByteCodeSize = spirv.size() * sizeof(uint32_t);
  device->CreateShader(info, &shader);
  return shader;
}

}  // namespace

void RecordAdsBlend(float blend) {
  g_ads_blend.store(std::clamp(blend, 0.0f, 1.0f), std::memory_order_relaxed);
  g_ads_until.store((std::chrono::steady_clock::now() + std::chrono::milliseconds(250)).time_since_epoch().count(),
                    std::memory_order_relaxed);
}

float AdsBlendNow() {
  if (g_ads_until.load(std::memory_order_relaxed) < std::chrono::steady_clock::now().time_since_epoch().count()) return 0.0f;
  return g_ads_blend.load(std::memory_order_relaxed);
}

int TestDofShaders() {
  return int(CompileGlsl(GlslStage::kVertex, kVS, "depth of field VS").empty()) +
         int(CompileGlsl(GlslStage::kPixel, kPS, "depth of field PS").empty());
}

bool DepthOfField::Load(IRenderDevice* device) {
  device_ = device;
  vs_ = Shader(device, SHADER_TYPE_VERTEX, kVS, "NativeXRenderer depth of field VS");
  ps_ = Shader(device, SHADER_TYPE_PIXEL, kPS, "NativeXRenderer depth of field PS");
  BufferDesc constants;
  constants.Name = "NativeXRenderer depth of field constants";
  constants.Size = 32;
  constants.Usage = USAGE_DYNAMIC;
  constants.BindFlags = BIND_UNIFORM_BUFFER;
  constants.CPUAccessFlags = CPU_ACCESS_WRITE;
  device->CreateBuffer(constants, nullptr, &constants_);
  if (!vs_ || !ps_ || !constants_) {
    REXLOG_ERROR("[native_x_renderer] depth of field: its pass could not be set up");
    failed_ = true;
    return false;
  }
  return true;
}

bool DepthOfField::Apply(IDeviceContext* context, const Inputs& in) {
  if (failed_ || !device_ || !in.color || !in.depth || !in.depth_srv || !in.width || !in.height || !in.depth_width ||
      !in.depth_height || in.blend <= 0.01f) {
    return false;
  }
  const TextureDesc& color = in.color->GetDesc();
  if (!pso_ || format_ != color.Format) {
    pso_.Release(), srb_.Release();
    GraphicsPipelineStateCreateInfo info;
    info.PSODesc.Name = "NativeXRenderer depth of field";
    info.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
    info.GraphicsPipeline.NumRenderTargets = 1;
    info.GraphicsPipeline.RTVFormats[0] = color.Format;
    info.GraphicsPipeline.DSVFormat = TEX_FORMAT_UNKNOWN;
    info.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    info.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
    info.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;
    info.pVS = vs_;
    info.pPS = ps_;
    SamplerDesc point, linear;
    point.MinFilter = point.MagFilter = point.MipFilter = FILTER_TYPE_POINT;
    point.AddressU = point.AddressV = point.AddressW = TEXTURE_ADDRESS_CLAMP;
    linear.MinFilter = linear.MagFilter = linear.MipFilter = FILTER_TYPE_LINEAR;
    linear.AddressU = linear.AddressV = linear.AddressW = TEXTURE_ADDRESS_CLAMP;
    ImmutableSamplerDesc samplers[] = {{SHADER_TYPE_PIXEL, "colorTex", linear}, {SHADER_TYPE_PIXEL, "depthTex", point}};
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
    info.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
    info.PSODesc.ResourceLayout.NumImmutableSamplers = 2;
    device_->CreateGraphicsPipelineState(info, &pso_);
    if (pso_) pso_->CreateShaderResourceBinding(&srb_, true);
    format_ = color.Format;
    if (!srb_) {
      REXLOG_ERROR("[native_x_renderer] depth of field: no pipeline for the format {}", int(color.Format));
      failed_ = true;
      return false;
    }
  }
  if (!source_ || source_->GetDesc().Width != color.Width || source_->GetDesc().Height != color.Height ||
      source_->GetDesc().Format != color.Format) {
    TextureDesc desc;
    desc.Name = "NativeXRenderer depth of field source";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = color.Width;
    desc.Height = color.Height;
    desc.Format = color.Format;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    source_.Release();
    device_->CreateTexture(desc, nullptr, &source_);
    if (!source_) {
      failed_ = true;
      return false;
    }
  }
  // The lens: sharp from 1.4 m on, the most blur at 0.3 m and nearer, up to 1.1% of the view's height.
  const float radius = float(in.height) * 0.011f * in.blend;
  if (!said_) {
    said_ = true;
    REXLOG_INFO("[native_x_renderer] depth of field: the view {}x{} (depth {}x{}), {} taps, up to {:.1f} pixels", in.width,
                in.height, in.depth_width, in.depth_height, in.taps, float(in.height) * 0.011f);
  }
  {
    void* data = nullptr;
    context->MapBuffer(constants_, MAP_WRITE, MAP_FLAG_DISCARD, data);
    const float k[8] = {float(in.width), float(in.height), float(in.depth_width), float(in.depth_height),
                        1.4f, 0.3f, radius, float(std::clamp(in.taps, 6, 64))};
    std::memcpy(data, k, sizeof(k));
    context->UnmapBuffer(constants_, MAP_WRITE);
  }
  CopyTextureAttribs copy{in.color, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, source_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
  context->CopyTexture(copy);
  ITextureView* target = in.color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
  if (!target) return false;
  context->SetRenderTargets(1, &target, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  Viewport viewport;
  viewport.Width = float(color.Width);
  viewport.Height = float(color.Height);
  viewport.MaxDepth = 1.0f;
  context->SetViewports(1, &viewport, 0, 0);
  context->SetPipelineState(pso_);
  auto set = [&](const char* name, IDeviceObject* object) {
    if (auto* v = srb_->GetVariableByName(SHADER_TYPE_PIXEL, name)) v->Set(object);
  };
  set("DofConstants", constants_);
  set("colorTex", source_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
  set("depthTex", in.depth_srv);
  context->CommitShaderResources(srb_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  DrawAttribs draw;
  draw.NumVertices = 3;
  context->Draw(draw);
  context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
  return true;
}

}  // namespace native_x_renderer
