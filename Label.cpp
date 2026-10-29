#include "Label.hpp"
#include "Config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

// Window class (or desktop file name) -> Icon= value, from the .desktop files.
const std::unordered_map<std::string, std::string>& desktopIcons() {
    static const auto INDEX = [] {
        std::unordered_map<std::string, std::string> index;
        std::error_code                              ec;
        for (const auto& dir : dataDirs()) {
            for (fs::recursive_directory_iterator it(dir / "applications", ec), end; !ec && it != end; it.increment(ec)) {
                if (it->path().extension() != ".desktop")
                    continue;

                std::ifstream file(it->path());
                std::string   line, icon, wmClass;
                bool          inEntry = false;
                while (std::getline(file, line)) {
                    if (line.starts_with("[")) {
                        inEntry = line == "[Desktop Entry]";
                        continue;
                    }
                    if (inEntry && line.starts_with("Icon="))
                        icon = line.substr(5);
                    else if (inEntry && line.starts_with("StartupWMClass="))
                        wmClass = line.substr(15);
                }
                if (icon.empty())
                    continue;

                // First hit wins: user files are listed before system ones.
                index.emplace(lower(it->path().stem().string()), icon);
                if (!wmClass.empty())
                    index.emplace(lower(wmClass), icon);
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
    if (const auto it = desktopIcons().find(KEY); it != desktopIcons().end())
        file = findIconFile(it->second);
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

// ── texture ─────────────────────────────────────────────────────────────────

struct SEntry {
    PHLWINDOWREF         window;
    std::string          title, windowClass, family;
    int                  fontPx = 0;
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

SP<Render::ITexture> build(const std::string& title, const std::string& windowClass, const std::string& family, int fontPx) {
    const double PAD     = fontPx * 0.7;
    const double GAP     = fontPx * 0.5;
    const int    ICONPX  = std::max(8, sc<int>(std::round(fontPx * 1.5)));
    const double HEIGHT  = std::ceil(fontPx * 2.4);
    const int    MAXTEXT = fontPx * 22;

    const auto ICONFILE = iconFileFor(windowClass);
    const auto ICON     = ICONFILE.empty() ? nullptr : loadIcon(ICONFILE, ICONPX);

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

    const double ICONW  = ICON ? ICONPX + GAP : 0.0;
    const double WIDTH  = std::ceil(PAD * 2 + ICONW + textW);
    auto* const  surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sc<int>(WIDTH), sc<int>(HEIGHT));
    auto* const  cr      = cairo_create(surface);

    cairo_set_source_rgba(cr, 0.05, 0.05, 0.07, 0.72);
    roundedRect(cr, 0, 0, WIDTH, HEIGHT, HEIGHT / 2);
    cairo_fill(cr);

    double x = PAD;
    if (ICON && ICON->cairo()) {
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
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
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

SP<Render::ITexture> texture(const PHLWINDOW& window, float monitorScale, int fontSize) {
    if (!window || window->m_title.empty())
        return nullptr;

    const int  FONTPX = std::max(6, sc<int>(std::round(fontSize * monitorScale)));
    const auto FAMILY = ScrollOverview::Config::getValue<std::string>("misc:font_family");

    auto&      entries = cache();
    auto&      entry   = entries[window.get()];
    if (entry.tex && entry.window.lock() == window && entry.title == window->m_title && entry.windowClass == window->m_class && entry.family == FAMILY && entry.fontPx == FONTPX)
        return entry.tex;

    entry.window      = window;
    entry.title       = window->m_title;
    entry.windowClass = window->m_class;
    entry.family      = FAMILY;
    entry.fontPx      = FONTPX;
    entry.tex         = build(entry.title, entry.windowClass, FAMILY, FONTPX);

    // Windows come and go while the overview stays open.
    if (entries.size() > 64)
        std::erase_if(entries, [](const auto& item) { return item.second.window.expired(); });

    return entry.tex;
}

void clear() {
    cache().clear();
}

}
