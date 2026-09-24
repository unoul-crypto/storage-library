#include "game_storage/raylib_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace game_storage::ui {
namespace {
Rectangle native(Rect r) { return {r.x, r.y, r.width, r.height}; }
Rect intersection(Rect a, Rect b) {
    const float x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    return {x, y, std::max(0.0f, std::min(a.x + a.width, b.x + b.width) - x),
                  std::max(0.0f, std::min(a.y + a.height, b.y + b.height) - y)};
}
void fill(Rect r, Color color) { if (r.width > 0 && r.height > 0) DrawRectangleRec(native(r), color); }
void outline(Rect r, Color color) { if (r.width > 0 && r.height > 0) DrawRectangleLinesEx(native(r), 1, color); }
void clip_to(Rect r) {
    const int x = static_cast<int>(std::ceil(r.x)), y = static_cast<int>(std::ceil(r.y));
    BeginScissorMode(x, y, std::max(0, static_cast<int>(std::floor(r.x + r.width)) - x),
                           std::max(0, static_cast<int>(std::floor(r.y + r.height)) - y));
}
}
RaylibRenderer::RaylibRenderer(Theme theme, ImageResolver resolver)
    : theme_(theme), resolver_(std::move(resolver)) {}
RaylibRenderer::~RaylibRenderer() { clear_images(); }
void RaylibRenderer::clear_images() {
    for (const auto& pair : images_) if (pair.second.id) UnloadTexture(pair.second);
    images_.clear();
}
Texture2D RaylibRenderer::image(const std::string& key) {
    if (resolver_) return resolver_(key);
    const auto found = images_.find(key);
    if (found != images_.end()) return found->second;
    const auto texture = LoadTexture(key.c_str());
    images_.emplace(key, texture);
    return texture;
}
void RaylibRenderer::text(const std::string& value, Rect bounds, Color color, float size) {
    if (bounds.width <= 0 || bounds.height <= 0 || value.empty()) return;
    const Font font = theme_.font.texture.id ? theme_.font : GetFontDefault();
    std::istringstream lines(value);
    std::string line;
    float y = bounds.y;
    while (std::getline(lines, line) && y + size <= bounds.y + bounds.height) {
        if (MeasureTextEx(font, line.c_str(), size, 1).x > bounds.width) {
            while (!line.empty() && MeasureTextEx(font, (line + "...").c_str(), size, 1).x > bounds.width) {
                auto end = line.size() - 1;
                while (end > 0 && (static_cast<unsigned char>(line[end]) & 0xc0) == 0x80) --end;
                line.resize(end);
            }
            line += "...";
        }
        DrawTextEx(font, line.c_str(), {bounds.x, y}, size, 1, color);
        y += size + 2;
    }
}
void RaylibRenderer::cell(const Cell& content, Rect bounds, Rect clip, Color color) {
    const auto visible = intersection(bounds, clip);
    if (visible.width <= 0 || visible.height <= 0) return;
    clip_to(visible);
    float x = bounds.x + theme_.padding;
    if (!content.png.empty()) {
        const float size = std::max(0.0f, std::min(theme_.image_size, bounds.height - 8));
        const auto texture = image(content.png);
        const float y = bounds.y + (bounds.height - size) / 2;
        if (texture.id && texture.width > 0 && texture.height > 0) {
            const float scale = size / static_cast<float>(std::max(texture.width, texture.height));
            const float w = static_cast<float>(texture.width) * scale, h = static_cast<float>(texture.height) * scale;
            DrawTexturePro(texture, {0, 0, static_cast<float>(texture.width), static_cast<float>(texture.height)},
                           {x + (size - w) / 2, y + (size - h) / 2, w, h}, {0, 0}, 0, WHITE);
        } else {
            outline({x, y, size, size}, theme_.muted);
            DrawLineV({x, y}, {x + size, y + size}, theme_.muted);
        }
        x += size + theme_.padding;
    }
    const auto lines = static_cast<float>(1 + std::count(content.text.begin(), content.text.end(), '\n'));
    const float text_height = lines * (theme_.font_size + 2);
    text(content.text, {x, bounds.y + std::max(2.0f, (bounds.height - text_height) / 2),
                        std::max(0.0f, bounds.x + bounds.width - theme_.padding - x), bounds.height - 4}, color, theme_.font_size);
    EndScissorMode();
}
Input RaylibRenderer::poll_input() {
    const auto mouse = GetMousePosition();
    const auto wheel = GetMouseWheelMoveV();
    return {{mouse.x, mouse.y}, wheel.y, wheel.x, GetFrameTime(),
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT), IsMouseButtonDown(MOUSE_BUTTON_LEFT),
            IsMouseButtonPressed(MOUSE_BUTTON_RIGHT), IsKeyPressed(KEY_ESCAPE),
            IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL),
            IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT),
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT), IsKeyPressed(KEY_ENTER)};
}
void RaylibRenderer::custom(std::optional<std::size_t> panel, Rect bounds) {
    if (bounds.height <= 0) return;
    fill(bounds, theme_.header);
    if (!custom_drawer_ || bounds.width <= 0) return;
    clip_to(bounds);
    try { custom_drawer_(panel, bounds); }
    catch (...) { EndScissorMode(); throw; }
    EndScissorMode();
}
void RaylibRenderer::draw(const Frame& frame, Point mouse) {
    for (std::size_t panel_index = 0; panel_index < frame.panels.size(); ++panel_index) {
        const auto& panel = frame.panels[panel_index];
        fill(panel.bounds, theme_.panel);
        cell({panel.title, {}}, {panel.bounds.x, panel.bounds.y, panel.bounds.width, panel.header.y - panel.bounds.y}, panel.bounds, theme_.accent);
        fill(panel.header, theme_.header);
        float x = panel.header.x - panel.scroll_x;
        for (const auto& column : panel.columns) {
            cell({column.title, {}}, {x, panel.header.y, column.width, panel.header.height}, panel.header, theme_.muted);
            x += column.width;
        }
        for (const auto& row : panel.rows) {
            Color background = row.id % 2 ? theme_.alternate : theme_.panel;
            if (std::find(panel.selected_ids.begin(), panel.selected_ids.end(), row.id) != panel.selected_ids.end())
                background = theme_.selected;
            else if (panel.hovered == row.id) background = theme_.hover;
            fill(intersection(row.bounds, panel.body), background);
            x = panel.body.x - panel.scroll_x;
            for (std::size_t c = 0; c < row.cells.size(); ++c) {
                cell(row.cells[c], {x, row.bounds.y, panel.columns[c].width, row.bounds.height}, panel.body, theme_.text);
                x += panel.columns[c].width;
            }
        }
        if (!panel.total_rows) cell({"No items", {}}, {panel.body.x, panel.body.y, panel.body.width, 44}, panel.body, theme_.muted);
        fill(panel.horizontal_track, theme_.track); fill(panel.vertical_track, theme_.track);
        fill(panel.horizontal_thumb, theme_.thumb); fill(panel.vertical_thumb, theme_.thumb);
        custom(panel_index, panel.footer);
        outline(panel.bounds, theme_.border);
    }
    if (frame.shared_footer.height > 0) {
        custom(std::nullopt, frame.shared_footer);
        outline(frame.shared_footer, theme_.border);
    }
    if (frame.tooltip) {
        const auto& tip = *frame.tooltip;
        fill({tip.bounds.x + 4, tip.bounds.y + 4, tip.bounds.width, tip.bounds.height}, {0, 0, 0, 90});
        fill(tip.bounds, theme_.popup);
        for (std::size_t i = 0; i < tip.content.size(); ++i)
            cell(tip.content[i], {tip.bounds.x, tip.bounds.y + static_cast<float>(i) * tip.row_height, tip.bounds.width, tip.row_height}, tip.bounds, theme_.text);
        outline(tip.bounds, theme_.accent);
    }
    if (frame.menu) {
        const auto& menu = *frame.menu;
        fill({menu.bounds.x + 4, menu.bounds.y + 4, menu.bounds.width, menu.bounds.height}, {0, 0, 0, 90});
        fill(menu.bounds, theme_.popup);
        if (menu.entries.empty()) cell({"No actions", {}}, menu.bounds, menu.bounds, theme_.muted);
        for (const auto& entry : menu.entries) {
            if (entry.bounds.contains(mouse) && entry.action.enabled) fill(entry.bounds, theme_.hover);
            std::string label = entry.label;
            if (!entry.action.enabled && !entry.action.disabled_reason.empty()) label += "\n" + entry.action.disabled_reason;
            cell({label, {}}, entry.bounds, menu.bounds, entry.action.enabled ? theme_.text : theme_.muted);
        }
        if (menu.more_above) fill({menu.bounds.x, menu.bounds.y, menu.bounds.width, 3}, theme_.accent);
        if (menu.more_below) fill({menu.bounds.x, menu.bounds.y + menu.bounds.height - 3, menu.bounds.width, 3}, theme_.accent);
        outline(menu.bounds, theme_.accent);
    }
    if (frame.drag) {
        const auto& drag = *frame.drag;
        if (drag.destination_panel) outline(frame.panels[*drag.destination_panel].body, theme_.accent);
        const std::string label = std::to_string(drag.items.size()) + " item(s)";
        const Rect badge{drag.pointer.x + 14, drag.pointer.y + 14, 130, 30};
        fill(badge, theme_.popup); outline(badge, theme_.accent);
        cell({label, {}}, badge, badge, theme_.text);
    }
    if (frame.quantity) {
        const auto& q = *frame.quantity;
        fill(q.bounds, theme_.popup); outline(q.bounds, theme_.accent);
        cell({"Quantity: " + std::to_string(q.value) + " / " + std::to_string(q.maximum), {}},
             {q.bounds.x + 12, q.bounds.y + 8, q.bounds.width - 24, 32}, q.bounds, theme_.text);
        fill(q.track, theme_.track); fill(q.thumb, theme_.accent);
        for (const auto& button : {q.minus, q.plus, q.confirm, q.cancel}) {
            fill(button, button.contains(mouse) ? theme_.hover : theme_.header);
            outline(button, theme_.border);
        }
        cell({"-", {}}, q.minus, q.minus, theme_.text);
        cell({"+", {}}, q.plus, q.plus, theme_.text);
        cell({"Move", {}}, q.confirm, q.confirm, theme_.text);
        cell({"Cancel", {}}, q.cancel, q.cancel, theme_.text);
    }
}
} // namespace game_storage::ui
