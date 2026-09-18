#include "ImageEffects.hpp"

#include <RmlUi/Core/CompiledFilterShader.h>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Decorator.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StyleSheetSpecification.h>

#include <algorithm>
#include <memory>

namespace aurora::rmlui {
namespace {

Rml::PropertyId s_desaturation;
Rml::PropertyId s_fadeStart;
Rml::PropertyId s_fadeEnd;

enum class ImageFit { Fill, Contain, Cover };

struct ImageData {
  Rml::Geometry geometry{};
  Rml::CompiledShader shader{};
  Rml::Vector2f textureSize{};
  Rml::Vector2f origin{};
  Rml::Vector2f size{};
  Rml::Vector4f parameters{};
};

class ImageEffects final : public Rml::Decorator {
public:
  ImageEffects(Rml::Texture texture, ImageFit fit) : m_fit{fit} { AddTexture(texture); }

  Rml::DecoratorDataHandle GenerateElementData(Rml::Element* element, Rml::BoxArea paintArea) const override {
    auto* manager = element->GetRenderManager();
    if (manager == nullptr) {
      return INVALID_DECORATORDATAHANDLE;
    }
    const auto box = element->GetRenderBox(paintArea);
    auto* data = new ImageData{};
    data->origin = box.GetFillOffset();
    data->size = box.GetFillSize();
    update_geometry(element, *data);
    return reinterpret_cast<Rml::DecoratorDataHandle>(data);
  }

  void ReleaseElementData(Rml::DecoratorDataHandle handle) const override {
    delete reinterpret_cast<ImageData*>(handle);
  }

  void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle handle) const override {
    auto& data = *reinterpret_cast<ImageData*>(handle);
    if (data.textureSize != Rml::Vector2f{GetTexture().GetDimensions()}) {
      update_geometry(element, data);
    }
    const float start = std::clamp(element->GetProperty(s_fadeStart)->Get<float>(), 0.f, 1.f);
    const float end = std::clamp(element->GetProperty(s_fadeEnd)->Get<float>(), 0.f, 1.f);
    const Rml::Vector4f parameters{
        data.origin.y + data.size.y * start,
        data.origin.y + data.size.y * end,
        std::clamp(element->GetProperty(s_desaturation)->Get<float>(), 0.f, 1.f),
        end > start ? 1.f : 0.f,
    };
    if (parameters.z == 0.f && parameters.w == 0.f) {
      data.geometry.Render(element->GetAbsoluteOffset(Rml::BoxArea::Border), GetTexture());
      return;
    }
    if (!data.shader || parameters != data.parameters) {
      data.parameters = parameters;
      data.shader = element->GetRenderManager()->CompileShader(
          "image-effects", Rml::Dictionary{{"parameters", Rml::Variant{parameters}}});
    }
    if (data.shader) {
      data.geometry.Render(element->GetAbsoluteOffset(Rml::BoxArea::Border), GetTexture(), data.shader);
    }
  }

private:
  ImageFit m_fit;

  void update_geometry(Rml::Element* element, ImageData& data) const {
    data.textureSize = Rml::Vector2f{GetTexture().GetDimensions()};
    Rml::Mesh mesh{};
    if (data.size.x > 0.f && data.size.y > 0.f && data.textureSize.x > 0.f && data.textureSize.y > 0.f) {
      auto size = data.size;
      auto offset = data.origin;
      Rml::Vector2f uvMin{0.f, 0.f};
      Rml::Vector2f uvMax{1.f, 1.f};
      if (m_fit != ImageFit::Fill) {
        const auto scale = data.size / data.textureSize;
        const float factor = m_fit == ImageFit::Cover ? std::max(scale.x, scale.y) : std::min(scale.x, scale.y);
        const auto imageSize = data.textureSize * factor;
        const auto imageOffset = ((data.size - imageSize) * 0.5f).Round();
        const Rml::Vector2f clippedOffset{std::max(imageOffset.x, 0.f), std::max(imageOffset.y, 0.f)};
        const Rml::Vector2f clippedEnd{std::min(imageOffset.x + imageSize.x, data.size.x),
                                       std::min(imageOffset.y + imageSize.y, data.size.y)};
        offset += clippedOffset;
        size = clippedEnd - clippedOffset;
        uvMin = (clippedOffset - imageOffset) / imageSize;
        uvMax = (clippedEnd - imageOffset) / imageSize;
      }
      Rml::Math::SnapToPixelGrid(offset, size);
      const auto& computed = element->GetComputedValues();
      Rml::MeshUtilities::GenerateQuad(mesh, offset, size, computed.image_color().ToPremultiplied(computed.opacity()),
                                       uvMin, uvMax);
    }
    data.geometry = element->GetRenderManager()->MakeGeometry(std::move(mesh));
  }
};

class ImageEffectsInstancer final : public Rml::DecoratorInstancer {
public:
  ImageEffectsInstancer() {
    m_source = RegisterProperty("source", "").AddParser("string").GetId();
    m_fit = RegisterProperty("fit", "fill").AddParser("keyword", "fill, contain, cover").GetId();
    RegisterShorthand("decorator", "source, fit", Rml::ShorthandType::FallThrough);
  }

  Rml::SharedPtr<Rml::Decorator>
  InstanceDecorator(const Rml::String&, const Rml::PropertyDictionary& properties,
                    const Rml::DecoratorInstancerInterface& instancerInterface) override {
    auto texture = instancerInterface.GetTexture(properties.GetProperty(m_source)->Get<Rml::String>());
    if (!texture) {
      return nullptr;
    }
    return Rml::MakeShared<ImageEffects>(texture, static_cast<ImageFit>(properties.GetProperty(m_fit)->Get<int>()));
  }

private:
  Rml::PropertyId m_source;
  Rml::PropertyId m_fit;
};

} // namespace

void register_image_effects() {
  s_desaturation =
      Rml::StyleSheetSpecification::RegisterProperty("image-desaturation", "0", true).AddParser("number").GetId();
  s_fadeStart =
      Rml::StyleSheetSpecification::RegisterProperty("image-fade-start", "1", false).AddParser("number").GetId();
  s_fadeEnd = Rml::StyleSheetSpecification::RegisterProperty("image-fade-end", "1", false).AddParser("number").GetId();
  static std::unique_ptr<ImageEffectsInstancer> instancer{};
  instancer = std::make_unique<ImageEffectsInstancer>();
  Rml::Factory::RegisterDecoratorInstancer("image-effects", instancer.get());
}

} // namespace aurora::rmlui
