#pragma once

#include "RmlUi/Core/Filter.h"
#include "RmlUi/Core/ID.h"
#include "RmlUi/Core/NumericValue.h"
#include "RmlUi/Core/Types.h"

namespace aurora::rmlui {

// backdrop-filter: glass(<bezel> <refraction> <specular> <tint> <saturation> <profile> <dome> <edges>);
// - bezel: width of the refracting band along the border
// - refraction: maximum displacement at the rim (also how far the filter reads outside the element)
// - specular: rim light strength. See set_glass_light_direction for angle
// - tint: background tint, fading out towards the rim
// - saturation: multiplier applied to the backdrop
// - profile: bezel cross-section. `convex` (squircle), `lip` (raised rim), or [0, 1] to blend
// - dome: backdrop magnification or minimization
// - edges: which edges get a bezel. `auto` (default) masks edges flush against the viewport, `all`, or a bitmask:
//   top = 1, right = 2, bottom = 4, left = 8
class GlassFilter : public Rml::Filter {
public:
  bool Initialise(Rml::NumericValue bezel, Rml::NumericValue refraction, float specular, Rml::Colourb tint,
                  float saturation, float profile, float dome, float edges);

  Rml::CompiledFilter CompileFilter(Rml::Element* element) const override;
  void ExtendInkOverflow(Rml::Element* element, Rml::Rectanglef& scissor_region) const override;

private:
  Rml::NumericValue m_bezel;
  Rml::NumericValue m_refraction;
  float m_specular = 0.f;
  Rml::Colourb m_tint;
  float m_saturation = 1.f;
  float m_profile = 0.f;
  float m_dome = 0.f;
  float m_edges = -1.f;
};

class GlassFilterInstancer : public Rml::FilterInstancer {
public:
  GlassFilterInstancer();

  Rml::SharedPtr<Rml::Filter> InstanceFilter(const Rml::String& name,
                                             const Rml::PropertyDictionary& properties) override;

private:
  struct PropertyIds {
    Rml::PropertyId bezel;
    Rml::PropertyId refraction;
    Rml::PropertyId specular;
    Rml::PropertyId tint;
    Rml::PropertyId saturation;
    Rml::PropertyId profile;
    Rml::PropertyId dome;
    Rml::PropertyId edges;
  };
  PropertyIds ids{};
};

} // namespace aurora::rmlui
