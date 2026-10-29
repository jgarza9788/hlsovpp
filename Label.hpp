#pragma once

// Window labels for the overview (hlsovpp): the app icon and the window title
// on a small pill. Built once per window as a single texture and cached until
// the title, class or size changes.

#include "Config.hpp"
#include "globals.hpp"
#include <hyprland/src/render/Renderer.hpp>

namespace OverviewLabel {

// Label texture in monitor pixels, or null when there is nothing to show.
// `style` is the user's label styling (colors resolve here, "accent" = the Omarchy theme accent).
SP<Render::ITexture> texture(const PHLWINDOW& window, float monitorScale, int fontSize, const ScrollOverview::Config::SLabelStyle& style);

// Forget every cached label (the overview closed).
void clear();

}
