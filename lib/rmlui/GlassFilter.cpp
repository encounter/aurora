#include "GlassFilter.hpp"

#include "RmlUi/Core/CompiledFilterShader.h"
#include "RmlUi/Core/ComputedValues.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/PropertyDefinition.h"
#include "RmlUi/Core/PropertyDictionary.h"
#include "RmlUi/Core/RenderManager.h"

namespace aurora::rmlui {

bool GlassFilter::Initialise(Rml::NumericValue bezel, Rml::NumericValue refraction, float specular, Rml::Colourb tint,
                             float saturation, float profile, float dome, float edges) {
  m_bezel = bezel;
  m_refraction = refraction;
  m_specular = specular;
  m_tint = tint;
  m_saturation = saturation;
  m_profile = profile;
  m_dome = dome;
  m_edges = edges;
  return Any(bezel.unit & Rml::Unit::LENGTH) && Any(refraction.unit & Rml::Unit::LENGTH);
}

Rml::CompiledFilter GlassFilter::CompileFilter(Rml::Element* element) const {
  const auto radii = element->GetComputedValues().border_radius();
  return element->GetRenderManager()->CompileFilter(
      "glass", Rml::Dictionary{
                   {"bezel", Rml::Variant(element->ResolveLength(m_bezel))},
                   {"refraction", Rml::Variant(element->ResolveLength(m_refraction))},
                   {"specular", Rml::Variant(m_specular)},
                   {"tint", Rml::Variant(m_tint)},
                   {"saturation", Rml::Variant(m_saturation)},
                   {"profile", Rml::Variant(m_profile)},
                   {"dome", Rml::Variant(m_dome)},
                   {"edges", Rml::Variant(m_edges)},
                   {"rect_size", Rml::Variant(element->GetBox().GetSize(Rml::BoxArea::Border))},
                   {"radii", Rml::Variant(Rml::Vector4f{radii[0], radii[1], radii[2], radii[3]})},
               });
}

void GlassFilter::ExtendInkOverflow(Rml::Element* element, Rml::Rectanglef& scissor_region) const {
  scissor_region = scissor_region.Extend(element->ResolveLength(m_refraction) + 1.f);
}

GlassFilterInstancer::GlassFilterInstancer() {
  ids.bezel = RegisterProperty("bezel", "30dp").AddParser("length").GetId();
  ids.refraction = RegisterProperty("refraction", "35dp").AddParser("length").GetId();
  ids.specular = RegisterProperty("specular", "0.3").AddParser("number").GetId();
  ids.tint = RegisterProperty("tint", "transparent").AddParser("color").GetId();
  ids.saturation = RegisterProperty("saturation", "1").AddParser("number").GetId();
  ids.profile = RegisterProperty("profile", "convex").AddParser("keyword", "convex, lip").AddParser("number").GetId();
  ids.dome = RegisterProperty("dome", "0").AddParser("number").GetId();
  ids.edges = RegisterProperty("edges", "auto").AddParser("keyword", "auto=-1, all=15").AddParser("number").GetId();
  RegisterShorthand("filter", "bezel, refraction, specular, tint, saturation, profile, dome, edges",
                    Rml::ShorthandType::FallThrough);
}

Rml::SharedPtr<Rml::Filter> GlassFilterInstancer::InstanceFilter(const Rml::String& /*name*/,
                                                                 const Rml::PropertyDictionary& properties) {
  const Rml::Property* bezel = properties.GetProperty(ids.bezel);
  const Rml::Property* refraction = properties.GetProperty(ids.refraction);
  const Rml::Property* specular = properties.GetProperty(ids.specular);
  const Rml::Property* tint = properties.GetProperty(ids.tint);
  const Rml::Property* saturation = properties.GetProperty(ids.saturation);
  const Rml::Property* profile = properties.GetProperty(ids.profile);
  const Rml::Property* dome = properties.GetProperty(ids.dome);
  const Rml::Property* edges = properties.GetProperty(ids.edges);
  if (bezel == nullptr || refraction == nullptr || specular == nullptr || tint == nullptr || saturation == nullptr ||
      profile == nullptr || dome == nullptr || edges == nullptr) {
    return nullptr;
  }

  auto filter = Rml::MakeShared<GlassFilter>();
  if (filter->Initialise(bezel->GetNumericValue(), refraction->GetNumericValue(), specular->Get<float>(),
                         tint->Get<Rml::Colourb>(), saturation->Get<float>(), profile->Get<float>(), dome->Get<float>(),
                         edges->Get<float>())) {
    return filter;
  }
  return nullptr;
}

} // namespace aurora::rmlui
