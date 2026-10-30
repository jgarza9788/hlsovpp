#include "Label.hpp"
#include "Config.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <pango/pangocairo.h>
#include <hyprgraphics/image/Image.hpp>
#include <hyprland/src/desktop/view/Window.hpp>

namespace OverviewLabel {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string envOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

std::vector<fs::path> splitDirs(const std::string& list) {
    std::vector<fs::path> out;
    size_t                start = 0;
    while (start <= list.size()) {
        const auto end = list.find(':', start);
        const auto part = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty())
            out.emplace_back(part);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return out;
}

std::vector<fs::path> dataDirs() {
    const std::string HOME = envOr("HOME", "");
    std::vector<fs::path> dirs{envOr("XDG_DATA_HOME", HOME + "/.local/share")};
    for (const auto& dir : splitDirs(envOr("XDG_DATA_DIRS", "/usr/local/share:/usr/share")))
        dirs.push_back(dir);
    return dirs;
}

// ── icons ───────────────────────────────────────────────────────────────────

struct SDesktopEntry {
    std::string icon, name;
};

// Window class (or desktop file name) -> Icon= and Name= values, from the .desktop files.
const std::unordered_map<std::string, SDesktopEntry>& desktopEntries() {
    static const auto INDEX = [] {
        std::unordered_map<std::string, SDesktopEntry> index;
        std::error_code                                ec;
        for (const auto& dir : dataDirs()) {
            for (fs::recursive_directory_iterator it(dir / "applications", ec), end; !ec && it != end; it.increment(ec)) {
                if (it->path().extension() != ".desktop")
                    continue;

                std::ifstream file(it->path());
                std::string   line, icon, name, wmClass;
                bool          inEntry = false;
                while (std::getline(file, line)) {
                    if (line.starts_with("[")) {
                        inEntry = line == "[Desktop Entry]";
                        continue;
                    }
                    if (inEntry && line.starts_with("Icon="))
                        icon = line.substr(5);
                    else if (inEntry && line.starts_with("Name="))
                        name = line.substr(5);
                    else if (inEntry && line.starts_with("StartupWMClass="))
                        wmClass = line.substr(15);
                }
                if (icon.empty())
                    continue;

                // First hit wins: user files are listed before system ones.
                index.emplace(lower(it->path().stem().string()), SDesktopEntry{icon, name});
                if (!wmClass.empty())
                    index.emplace(lower(wmClass), SDesktopEntry{icon, name});
            }
        }
        return index;
    }();
    return INDEX;
}

std::string gtkIconTheme() {
    std::ifstream file(envOr("XDG_CONFIG_HOME", envOr("HOME", "") + "/.config") + "/gtk-3.0/settings.ini");
    std::string   line;
    while (std::getline(file, line)) {
        if (line.starts_with("gtk-icon-theme-name="))
            return line.substr(20);
    }
    return "";
}

// File for an icon name: the active theme first, then hicolor, then pixmaps.
// Scalable wins, otherwise the largest PNG.
std::string findIconFile(const std::string& name) {
    std::error_code ec;
    if (name.starts_with("/"))
        return fs::exists(name, ec) ? name : "";

    std::vector<std::string> themes;
    if (const auto theme = gtkIconTheme(); !theme.empty())
        themes.push_back(theme);
    themes.emplace_back("hicolor");

    const std::vector<std::string> SUBDIRS = {"scalable/apps", "512x512/apps", "256x256/apps", "128x128/apps", "96x96/apps", "64x64/apps", "48x48/apps", "32x32/apps"};

    auto roots = dataDirs();
    for (auto& root : roots)
        root /= "icons";
    roots.insert(roots.begin(), fs::path(envOr("HOME", "")) / ".icons");

    for (const auto& theme : themes) {
        for (const auto& sub : SUBDIRS) {
            for (const auto& root : roots) {
                for (const auto* ext : {".svg", ".png"}) {
                    const auto path = root / theme / sub / (name + ext);
                    if (fs::exists(path, ec))
                        return path.string();
                }
            }
        }
    }

    for (const auto& root : dataDirs()) {
        for (const auto* ext : {".svg", ".png"}) {
            const auto path = root / "pixmaps" / (name + ext);
            if (fs::exists(path, ec))
                return path.string();
        }
    }
    return "";
}

std::string iconFileFor(const std::string& windowClass) {
    static std::unordered_map<std::string, std::string> cache;

    const auto KEY = lower(windowClass);
    if (const auto it = cache.find(KEY); it != cache.end())
        return it->second;

    std::string file;
    if (const auto it = desktopEntries().find(KEY); it != desktopEntries().end())
        file = findIconFile(it->second.icon);
    if (file.empty())
        file = findIconFile(KEY);

    cache.emplace(KEY, file);
    return file;
}

SP<Hyprgraphics::CCairoSurface> loadIcon(const std::string& path, int px) {
    static std::unordered_map<std::string, SP<Hyprgraphics::CCairoSurface>> cache;

    const auto KEY = path + "@" + std::to_string(px);
    if (const auto it = cache.find(KEY); it != cache.end())
        return it->second;

    SP<Hyprgraphics::CCairoSurface> surface;
    Hyprgraphics::CImage            image(path, {sc<double>(px), sc<double>(px)});
    if (image.success())
        surface = image.cairoSurface();

    cache.emplace(KEY, surface);
    return surface;
}

// ── accent ──────────────────────────────────────────────────────────────────

// The Omarchy theme accent as 0xRRGGBB, resolved like the shell's Commons.Color:
// the `accent` key of theme/colors.toml, else `color4`. Re-read every couple of
// seconds so a theme switch shows up without reopening anything.
uint32_t accentColor() {
    constexpr uint32_t FALLBACK = 0x33ccff;
    static uint32_t    rgb      = FALLBACK;
    static auto        checked  = std::chrono::steady_clock::time_point{};

    const auto NOW = std::chrono::steady_clock::now();
    if (checked != std::chrono::steady_clock::time_point{} && NOW - checked < std::chrono::seconds(2))
        return rgb;
    checked = NOW;

    std::ifstream file(envOr("XDG_STATE_HOME", envOr("HOME", "") + "/.local/state") + "/omarchy/current/theme/colors.toml");
    std::string   line;
    std::optional<uint32_t> accent, color4;
    while (std::getline(file, line)) {
        const auto EQ = line.find('=');
        if (EQ == std::string::npos)
            continue;
        auto key = line.substr(0, EQ);
        key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) { return std::isspace(c); }), key.end());
        if (key != "accent" && key != "color4")
            continue;
        const auto HASH = line.find('#', EQ);
        if (HASH == std::string::npos || line.size() < HASH + 7)
            continue;
        try {
            (key == "accent" ? accent : color4) = sc<uint32_t>(std::stoul(line.substr(HASH + 1, 6), nullptr, 16));
        } catch (...) {}
    }
    rgb = accent.value_or(color4.value_or(FALLBACK));
    return rgb;
}

// "#rrggbb" or "accent" -> 0xRRGGBB
uint32_t resolveColor(const std::string& spec, uint32_t fallback) {
    if (spec == "accent")
        return accentColor();
    const auto HEX = !spec.empty() && spec[0] == '#' ? spec.substr(1) : spec;
    if (HEX.size() != 6 || HEX.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return fallback;
    return sc<uint32_t>(std::stoul(HEX, nullptr, 16));
}

// ── title ───────────────────────────────────────────────────────────────────

std::string alnum(const std::string& s) {
    std::string out;
    for (const unsigned char c : s) {
        if (std::isalnum(c) || c >= 0x80)
            out += sc<char>(std::tolower(c));
    }
    return out;
}

// Drop the app name a title carries ("Page - Zen Browser", "Zen Browser | Page"): the icon
// already says it. A part counts as the app name when it matches the class or the desktop
// entry's Name=. Never strips down to nothing.
std::string cleanTitle(const std::string& title, const std::string& windowClass) {
    static const std::vector<std::string> SEPS = {" - ", " \u2013 ", " \u2014 ", " | ", " \u00b7 "};

    std::vector<std::string> names{alnum(windowClass)};
    if (const auto it = desktopEntries().find(lower(windowClass)); it != desktopEntries().end())
        names.push_back(alnum(it->second.name));

    const auto isApp = [&](const std::string& part) {
        const auto P = alnum(part);
        if (P.size() < 3)
            return false;
        return std::ranges::any_of(names, [&](const std::string& n) { return n.size() >= 3 && (P == n || (P.size() >= 4 && n.find(P) != std::string::npos) || (n.size() >= 4 && P.find(n) != std::string::npos)); });
    };

    size_t bestPos = std::string::npos, bestLen = 0;
    for (const auto& sep : SEPS) {
        const auto POS = title.rfind(sep);
        if (POS != std::string::npos && (bestPos == std::string::npos || POS > bestPos))
            bestPos = POS, bestLen = sep.size();
    }
    if (bestPos != std::string::npos && isApp(title.substr(bestPos + bestLen)))
        return title.substr(0, bestPos);

    bestPos = std::string::npos;
    for (const auto& sep : SEPS) {
        const auto POS = title.find(sep);
        if (POS != std::string::npos && (bestPos == std::string::npos || POS < bestPos))
            bestPos = POS, bestLen = sep.size();
    }
    if (bestPos != std::string::npos && isApp(title.substr(0, bestPos)))
        return title.substr(bestPos + bestLen);

    return title;
}

// ── texture ─────────────────────────────────────────────────────────────────

struct SStyle {
    bool     icon    = true;
    bool     cleanTitle = true;
    double   radius  = 1.0;
    uint32_t bg      = 0x0d0d12;
    double   bgAlpha = 0.72;
    uint32_t text    = 0xffffff;
    int64_t  border  = -1; // -1 none, else 0xRRGGBB
    int      borderWidth = 0;
    bool operator==(const SStyle&) const = default;
};

void setRgb(cairo_t* cr, uint32_t rgb, double alpha) {
    cairo_set_source_rgba(cr, ((rgb >> 16) & 0xff) / 255.0, ((rgb >> 8) & 0xff) / 255.0, (rgb & 0xff) / 255.0, alpha);
}

struct SEntry {
    PHLWINDOWREF         window;
    std::string          title, windowClass, family;
    int                  fontPx = 0;
    SStyle               style;
    SP<Render::ITexture> tex;
};

void roundedRect(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

SP<Render::ITexture> build(const std::string& title, const std::string& windowClass, const std::string& family, int fontPx, const SStyle& style) {
    const double PAD     = fontPx * 0.7;
    const double GAP     = fontPx * 0.5;
    const int    ICONPX  = std::max(8, sc<int>(std::round(fontPx * 1.5)));
    const double HEIGHT  = std::ceil(fontPx * 2.4);
    const int    MAXTEXT = fontPx * 22;

    const auto ICONFILE = style.icon ? iconFileFor(windowClass) : std::string{};
    const auto ICON     = ICONFILE.empty() ? nullptr : loadIcon(ICONFILE, ICONPX);
    const bool BADGE    = style.icon && !(ICON && ICON->cairo()); // no icon found: a letter badge keeps the pills aligned

    auto* const measure = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    auto* const mcr     = cairo_create(measure);
    auto* const layout  = pango_cairo_create_layout(mcr);

    auto* const font = pango_font_description_from_string(family.c_str());
    pango_font_description_set_absolute_size(font, fontPx * PANGO_SCALE);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_text(layout, title.c_str(), -1);
    pango_layout_set_width(layout, MAXTEXT * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_single_paragraph_mode(layout, true);

    int textW = 0, textH = 0;
    pango_layout_get_pixel_size(layout, &textW, &textH);

    const double ICONW  = style.icon ? ICONPX + GAP : 0.0;
    const double WIDTH  = std::ceil(PAD * 2 + ICONW + textW);
    auto* const  surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sc<int>(WIDTH), sc<int>(HEIGHT));
    auto* const  cr      = cairo_create(surface);

    const double RADIUS = HEIGHT / 2 * style.radius;
    setRgb(cr, style.bg, style.bgAlpha);
    roundedRect(cr, 0, 0, WIDTH, HEIGHT, RADIUS);
    cairo_fill(cr);

    if (style.border >= 0) {
        const double LINE = style.borderWidth > 0 ? style.borderWidth : std::max(1.5, fontPx * 0.12);
        setRgb(cr, sc<uint32_t>(style.border), 1.0);
        cairo_set_line_width(cr, LINE);
        roundedRect(cr, LINE / 2, LINE / 2, WIDTH - LINE, HEIGHT - LINE, std::max(0.0, RADIUS - LINE / 2));
        cairo_stroke(cr);
    }

    double x = PAD;
    if (BADGE) {
        const char letter[2] = {[&] {
            for (const unsigned char c : windowClass) {
                if (std::isalnum(c) && c < 0x80)
                    return sc<char>(std::toupper(c));
            }
            return '?';
        }(), 0};

        setRgb(cr, accentColor(), 0.9);
        cairo_arc(cr, x + ICONPX / 2.0, HEIGHT / 2, ICONPX / 2.0, 0, 2 * M_PI);
        cairo_fill(cr);

        auto* const glyph = pango_cairo_create_layout(cr);
        auto* const gfont = pango_font_description_from_string(family.c_str());
        pango_font_description_set_absolute_size(gfont, ICONPX * 0.6 * PANGO_SCALE);
        pango_font_description_set_weight(gfont, PANGO_WEIGHT_BOLD);
        pango_layout_set_font_description(glyph, gfont);
        pango_font_description_free(gfont);
        pango_layout_set_text(glyph, letter, 1);
        int gw = 0, gh = 0;
        pango_layout_get_pixel_size(glyph, &gw, &gh);
        setRgb(cr, style.bg, 1.0);
        cairo_move_to(cr, x + (ICONPX - gw) / 2.0, (HEIGHT - gh) / 2.0);
        pango_cairo_show_layout(cr, glyph);
        g_object_unref(glyph);
        x += ICONW;
    } else if (ICON && ICON->cairo()) {
        const auto SIZE = ICON->size();
        if (SIZE.x > 0 && SIZE.y > 0) {
            const double S = std::min(ICONPX / SIZE.x, ICONPX / SIZE.y);
            cairo_save(cr);
            cairo_translate(cr, x + (ICONPX - SIZE.x * S) / 2, (HEIGHT - SIZE.y * S) / 2);
            cairo_scale(cr, S, S);
            cairo_set_source_surface(cr, ICON->cairo(), 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
        }
        x += ICONW;
    }

    pango_cairo_update_layout(cr, layout);
    setRgb(cr, style.text, 0.95);
    cairo_move_to(cr, x, (HEIGHT - textH) / 2);
    pango_cairo_show_layout(cr, layout);

    cairo_surface_flush(surface);
    auto tex = g_pHyprRenderer->createTexture(surface);

    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    cairo_destroy(mcr);
    cairo_surface_destroy(measure);
    return tex;
}

std::unordered_map<const void*, SEntry>& cache() {
    static std::unordered_map<const void*, SEntry> entries;
    return entries;
}

}

SP<Render::ITexture> texture(const PHLWINDOW& window, float monitorScale, int fontSize, const ScrollOverview::Config::SLabelStyle& userStyle) {
    if (!window || (window->m_title.empty() && window->m_class.empty()))
        return nullptr;

    const int  FONTPX = std::max(6, sc<int>(std::round(fontSize * monitorScale)));
    const auto FAMILY = ScrollOverview::Config::getValue<std::string>("misc:font_family");
    const SStyle STYLE = {
        .icon        = userStyle.icon,
        .cleanTitle  = userStyle.cleanTitle,
        .radius      = userStyle.radius,
        .bg          = resolveColor(userStyle.background, 0x0d0d12),
        .bgAlpha     = userStyle.backgroundOpacity,
        .text        = resolveColor(userStyle.text, 0xffffff),
        .border      = userStyle.border ? sc<int64_t>(resolveColor(userStyle.borderColor, accentColor())) : -1,
        .borderWidth = userStyle.borderWidth,
    };

    // Empty titles show the class; otherwise the app name is trimmed unless the user wants it.
    const std::string TITLE = window->m_title.empty() ? window->m_class : (STYLE.cleanTitle ? cleanTitle(window->m_title, window->m_class) : window->m_title);

    auto&      entries = cache();
    auto&      entry   = entries[window.get()];
    if (entry.tex && entry.window.lock() == window && entry.title == TITLE && entry.windowClass == window->m_class && entry.family == FAMILY && entry.fontPx == FONTPX && entry.style == STYLE)
        return entry.tex;

    entry.window      = window;
    entry.title       = TITLE;
    entry.windowClass = window->m_class;
    entry.family      = FAMILY;
    entry.fontPx      = FONTPX;
    entry.style       = STYLE;
    entry.tex         = build(entry.title, entry.windowClass, FAMILY, FONTPX, STYLE);

    // Windows come and go while the overview stays open.
    if (entries.size() > 64)
        std::erase_if(entries, [](const auto& item) { return item.second.window.expired(); });

    return entry.tex;
}

void clear() {
    cache().clear();
}

}
