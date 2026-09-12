#include "game_storage/ui.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace game_storage::ui {
namespace {
float clamp(float v, float max) { return std::clamp(v, 0.0f, std::max(0.0f, max)); }
Rect popup(Rect area, Point anchor, float width, float height) {
    width = std::min(width, area.width);
    height = std::min(height, area.height);
    return {std::clamp(anchor.x, area.x, area.x + area.width - width),
            std::clamp(anchor.y, area.y, area.y + area.height - height), width, height};
}
Rect thumb(Rect track, float offset, float maximum, float viewport, bool horizontal) {
    if (maximum <= 0) return {};
    const float length = horizontal ? track.width : track.height;
    const float size = std::min(length, std::max(20.0f, length * viewport / (viewport + maximum)));
    const float position = (length - size) * offset / maximum;
    return horizontal ? Rect{track.x + position, track.y, size, track.height}
                      : Rect{track.x, track.y + position, track.width, size};
}
}
bool Rect::contains(Point p) const noexcept {
    return width > 0 && height > 0 && p.x >= x && p.y >= y && p.x < x + width && p.y < y + height;
}
View::View(Metrics metrics) : metrics_(metrics) {
    for (float value : {metrics.title_height, metrics.header_height, metrics.row_height,
                       metrics.scrollbar, metrics.wheel_step, metrics.tooltip_width,
                       metrics.tooltip_row_height, metrics.menu_width, metrics.menu_row_height}) {
        if (!std::isfinite(value) || value <= 0) throw std::invalid_argument("UI dimensions must be positive and finite");
    }
    if (!std::isfinite(metrics.gap) || metrics.gap < 0 ||
        !std::isfinite(metrics.tooltip_delay) || metrics.tooltip_delay < 0)
        throw std::invalid_argument("Invalid UI spacing or tooltip delay");
}
void View::set_panels(std::vector<Panel> panels) {
    if (panels.size() > 2) throw std::invalid_argument("A view supports at most two storages");
    for (const auto& panel : panels) {
        if (!panel.storage) throw std::invalid_argument("Panel storage is null");
        for (const auto& column : panel.table.columns)
            if (!std::isfinite(column.width) || column.width <= 0)
                throw std::invalid_argument("Column width must be positive and finite");
    }
    panels_ = std::move(panels);
    states_.assign(panels_.size(), {});
    hover_.reset(); menu_.reset(); drag_.reset(); last_action_.reset();
    hover_time_ = 0; menu_offset_ = 0; frame_ = {};
}
void View::layout(Rect bounds, const Context& context) {
    frame_.panels.clear();
    if (panels_.empty()) return;
    const float gap = panels_.size() == 2 ? std::min(metrics_.gap, bounds.width) : 0;
    const float width = (bounds.width - gap) / static_cast<float>(panels_.size());
    for (std::size_t p = 0; p < panels_.size(); ++p) {
        const auto& panel = panels_[p];
        auto& state = states_[p];
        PanelFrame f;
        f.title = panel.title;
        f.bounds = {bounds.x + static_cast<float>(p) * (width + gap), bounds.y, width, bounds.height};
        const float title = std::min(metrics_.title_height, bounds.height);
        const float header = std::min(metrics_.header_height, bounds.height - title);
        const float bar_x = std::min(metrics_.scrollbar, width);
        const float bar_y = std::min(metrics_.scrollbar, bounds.height - title - header);
        f.header = {f.bounds.x, bounds.y + title, std::max(0.0f, width - bar_x), header};
        f.body = {f.bounds.x, f.header.y + header, f.header.width,
                  std::max(0.0f, bounds.height - title - header - bar_y)};
        f.horizontal_track = {f.body.x, f.body.y + f.body.height, f.body.width, bar_y};
        f.vertical_track = {f.body.x + f.body.width, f.body.y, bar_x, f.body.height};
        auto items = panel.storage->items();
        if (panel.table.order) {
            const auto ordered = panel.table.order(items, context);
            std::map<ItemId, Item> original;
            for (const auto& item : items) original.emplace(item.id(), item);
            items.clear();
            for (const auto& item : ordered) {
                const auto it = original.find(item.id());
                if (it != original.end()) { items.push_back(it->second); original.erase(it); }
            }
        }
        f.total_rows = items.size();
        f.columns = panel.table.columns;
        float content_width = 0;
        for (const auto& column : f.columns) content_width += column.width;
        f.max_x = std::max(0.0f, content_width - f.body.width);
        f.max_y = std::max(0.0f, static_cast<float>(items.size()) * metrics_.row_height - f.body.height);
        state.x = clamp(state.x, f.max_x); state.y = clamp(state.y, f.max_y);
        f.scroll_x = state.x; f.scroll_y = state.y;
        if (state.selected && std::none_of(items.begin(), items.end(), [&](const Item& item) {
                return item.id() == *state.selected;
            })) state.selected.reset();
        f.selected = state.selected;
        f.horizontal_thumb = thumb(f.horizontal_track, state.x, f.max_x, f.body.width, true);
        f.vertical_thumb = thumb(f.vertical_track, state.y, f.max_y, f.body.height, false);
        const auto first = static_cast<std::size_t>(state.y / metrics_.row_height);
        for (std::size_t r = first; r < items.size() && f.body.height > 0; ++r) {
            const float y = f.body.y + static_cast<float>(r) * metrics_.row_height - state.y;
            if (y >= f.body.y + f.body.height) break;
            RowFrame row{items[r].id(), {f.body.x, y, f.body.width, metrics_.row_height}, {}};
            for (const auto& column : f.columns)
                row.cells.push_back(column.content ? column.content(items[r], *panel.storage, context) : Cell{});
            f.rows.push_back(std::move(row));
        }
        frame_.panels.push_back(std::move(f));
    }
}
void View::layout_menu(Rect bounds, const Context& context) {
    frame_.menu.reset();
    if (!menu_) return;
    if (states_[menu_->panel].selected != menu_->item) { menu_.reset(); return; }
    const auto& panel = panels_[menu_->panel];
    const auto catalog = panel.storage->actions(menu_->item, context);
    // A filtered or removed item cannot keep a stale menu open.
    if (catalog.status != ActionStatus::ready) { menu_.reset(); return; }
    const auto slots = std::max<std::size_t>(1, static_cast<std::size_t>(bounds.height / metrics_.menu_row_height));
    const auto count = catalog.actions.size();
    menu_offset_ = std::min(menu_offset_, count > slots ? count - slots : 0);
    const auto visible = std::min(slots, count);
    MenuFrame f;
    f.bounds = popup(bounds, menu_anchor_, metrics_.menu_width,
                     static_cast<float>(std::max<std::size_t>(1, visible)) * metrics_.menu_row_height);
    f.more_above = menu_offset_ > 0; f.more_below = menu_offset_ + visible < count;
    for (std::size_t i = 0; i < visible; ++i) {
        const auto& action = catalog.actions[menu_offset_ + i];
        f.entries.push_back({{f.bounds.x, f.bounds.y + static_cast<float>(i) * metrics_.menu_row_height,
                              f.bounds.width, std::min(metrics_.menu_row_height, f.bounds.height - static_cast<float>(i) * metrics_.menu_row_height)},
                             action, panel.table.action_label ? panel.table.action_label(action, context) : action.id});
    }
    frame_.menu = std::move(f);
}
const Frame& View::update(Rect bounds, const Input& input, const Context& context) {
    for (float v : {bounds.x, bounds.y, bounds.width, bounds.height, input.mouse.x, input.mouse.y,
                    input.seconds, input.wheel_x, input.wheel_y})
        if (!std::isfinite(v)) throw std::invalid_argument("UI input must be finite");
    bounds.width = std::max(0.0f, bounds.width); bounds.height = std::max(0.0f, bounds.height);
    frame_.tooltip.reset(); last_action_.reset();
    frame_.captures_pointer = (!panels_.empty() && bounds.contains(input.mouse)) || menu_.has_value() || drag_.has_value();
    layout(bounds, context);
    if (input.escape) { menu_.reset(); hover_.reset(); hover_time_ = 0; drag_.reset(); }
    layout_menu(bounds, context);
    if (menu_ && frame_.menu) {
        hover_.reset(); hover_time_ = 0;
        if (frame_.menu->bounds.contains(input.mouse)) {
            if (input.wheel_y != 0) {
                const auto step = static_cast<std::size_t>(std::max(1.0f, std::abs(input.wheel_y)));
                menu_offset_ = input.wheel_y > 0 ? (menu_offset_ > step ? menu_offset_ - step : 0) : menu_offset_ + step;
                layout_menu(bounds, context);
            }
            if (input.left_pressed) {
                // Copy before executing: handlers can change the underlying storage.
                const auto entries = frame_.menu->entries;
                for (const auto& entry : entries) if (entry.bounds.contains(input.mouse)) {
                    const auto target = *menu_;
                    const auto result = panels_[target.panel].storage->execute_action(target.item, entry.action.id, context);
                    last_action_ = ActionEvent{target.panel, target.item, entry.action.id, result};
                    if (result.status != ActionStatus::disabled) menu_.reset();
                    layout(bounds, context); layout_menu(bounds, context);
                    break;
                }
            }
            return frame_;
        }
        if (input.left_pressed || input.right_pressed) { menu_.reset(); frame_.menu.reset(); }
        return frame_; // Dismissal never clicks through to another table.
    }
    if (!input.left_down) drag_.reset();
    if (drag_) {
        const auto& f = frame_.panels[drag_->panel];
        const auto track = drag_->horizontal ? f.horizontal_track : f.vertical_track;
        const auto handle = drag_->horizontal ? f.horizontal_thumb : f.vertical_thumb;
        const float travel = drag_->horizontal ? track.width - handle.width : track.height - handle.height;
        const float position = drag_->horizontal ? input.mouse.x - track.x : input.mouse.y - track.y;
        const float maximum = drag_->horizontal ? f.max_x : f.max_y;
        auto& offset = drag_->horizontal ? states_[drag_->panel].x : states_[drag_->panel].y;
        offset = travel > 0 ? clamp((position - drag_->offset) / travel * maximum, maximum) : 0;
    }
    for (std::size_t p = 0; p < frame_.panels.size(); ++p) {
        const auto& f = frame_.panels[p];
        if (f.bounds.contains(input.mouse) && !drag_) {
            states_[p].y = clamp(states_[p].y - input.wheel_y * metrics_.wheel_step, f.max_y);
            states_[p].x = clamp(states_[p].x - input.wheel_x * metrics_.wheel_step, f.max_x);
            if (input.left_pressed) {
                for (bool horizontal : {true, false}) {
                    const auto track = horizontal ? f.horizontal_track : f.vertical_track;
                    const auto handle = horizontal ? f.horizontal_thumb : f.vertical_thumb;
                    const float max = horizontal ? f.max_x : f.max_y;
                    if (max <= 0 || !track.contains(input.mouse)) continue;
                    const float pointer = horizontal ? input.mouse.x : input.mouse.y;
                    const float start = horizontal ? handle.x : handle.y;
                    const float size = horizontal ? handle.width : handle.height;
                    const float grab = handle.contains(input.mouse) ? pointer - start : size / 2;
                    drag_ = Drag{p, horizontal, grab};
                    const float travel = horizontal ? track.width - size : track.height - size;
                    const float origin = horizontal ? track.x : track.y;
                    auto& offset = horizontal ? states_[p].x : states_[p].y;
                    offset = travel > 0 ? clamp((pointer - origin - grab) / travel * max, max) : 0;
                }
            }
        }
    }
    layout(bounds, context);
    std::optional<Target> hit;
    if (!drag_ && !input.escape) for (std::size_t p = 0; p < frame_.panels.size(); ++p) {
        auto& f = frame_.panels[p];
        if (!f.body.contains(input.mouse)) continue;
        for (const auto& row : f.rows) if (row.bounds.contains(input.mouse)) {
            hit = Target{p, row.id}; f.hovered = row.id;
            if (input.left_pressed || input.right_pressed) { states_[p].selected = row.id; f.selected = row.id; }
            break;
        }
    }
    if (!hit || !hover_ || hit->panel != hover_->panel || hit->item != hover_->item ||
        input.wheel_y != 0 || input.wheel_x != 0) hover_time_ = 0;
    else hover_time_ += std::max(0.0f, input.seconds);
    hover_ = hit;
    if (hit && input.right_pressed) {
        menu_ = hit; menu_anchor_ = input.mouse; menu_offset_ = 0;
        hover_time_ = 0; layout_menu(bounds, context);
    } else if (hit && hover_time_ >= metrics_.tooltip_delay) {
        const auto& panel = panels_[hit->panel];
        const auto item = panel.storage->item(hit->item);
        if (item && panel.table.tooltip) {
            auto content = panel.table.tooltip(*item, *panel.storage, context);
            if (!content.empty()) {
                const float height = static_cast<float>(content.size()) * metrics_.tooltip_row_height;
                frame_.tooltip = TooltipFrame{popup(bounds, {input.mouse.x + 16, input.mouse.y + 20}, metrics_.tooltip_width, height),
                                               std::move(content), metrics_.tooltip_row_height};
            }
        }
    }
    return frame_;
}
} // namespace game_storage::ui
