#pragma once

// hlsovpp: 3D tilt for windows in flight.
//
// A tilted window is drawn flat (shadow, surface, border, decorations: the
// usual overview code, unchanged) into a scratch framebuffer, then put back
// on screen as a perspective-projected quad. Everything the window draws is
// in that one image, so its border and shadow tilt with it.
//
//   if (TiltRender::begin(monitor)) {   // flushes, then redirects drawing
//       ...draw the window as usual and flush...
//       TiltRender::end(monitor, card, axis, angle);
//   }

#include "globals.hpp"
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

namespace TiltRender {

// Redirect rendering into the scratch framebuffer (cleared). False when tilt
// can't be drawn here (rotated monitor, no framebuffer): draw flat instead.
bool begin(PHLMONITOR monitor);

// Back to the real target, and draw `card` (monitor px) from the scratch
// framebuffer, rotated by `angle` radians about `axis` (unit, screen plane)
// through its centre, in perspective.
void end(PHLMONITOR monitor, const CBox& card, const Vector2D& axis, float angle);

// Free the shader and framebuffer (plugin unload).
void destroy();

}

class CTiltPassElement : public IPassElement {
  public:
    struct SData {
        CBox     card;  // monitor px, flat
        Vector2D axis;  // unit, screen plane
        float    angle = 0.F;
    };

    explicit CTiltPassElement(const SData& data);
    ~CTiltPassElement() override = default;

    std::vector<UP<IPassElement>> draw() override;
    bool                          needsLiveBlur() override {
        return false;
    }
    bool needsPrecomputeBlur() override {
        return false;
    }
    std::optional<CBox> boundingBox() override;
    CRegion             opaqueRegion() override {
        return {};
    }
    bool disableSimplification() override {
        return true;
    }
    const char* passName() override {
        return "CTiltPassElement";
    }
    ePassElementType type() override {
        return EK_CUSTOM;
    }

  private:
    SData m_data;
};
