#pragma once

#include "storage.hpp"
#include <set>

namespace game_storage::ui {

struct Point { float x = 0, y = 0; };
struct Rect {
    float x = 0, y = 0, width = 0, height = 0;
    bool contains(Point p) const noexcept;
};

// PNG is an application-defined asset key (a path with the default renderer).
struct Cell { std::string text; std::string png; };
using Tooltip = std::vector<Cell>;
struct Column {
    std::string title;
    float width = 180;
    std::function<Cell(const Item&, const Storage&, const Context&)> content;
};
struct TableConfig {
    std::vector<Column> columns;
    // May reorder or filter the supplied snapshots. Foreign/duplicate IDs are ignored.
    std::function<std::vector<Item>(std::vector<Item>, const Context&)> order;
    std::function<Tooltip(const Item&, const Storage&, const Context&)> tooltip;
    std::function<std::string(const ActionInfo&, const Context&)> action_label;
};
struct Panel {
    Storage* storage = nullptr; // Must outlive the view and its update calls.
    std::string title;
    TableConfig table;
    float footer_height = 0; // Reserved for game-owned UI below this table.
};
struct Metrics {
    float title_height = 40, header_height = 32, row_height = 44;
    float scrollbar = 14, gap = 16, wheel_step = 44;
    float tooltip_delay = 0.45f, tooltip_width = 320;
    float tooltip_row_height = 44, menu_width = 300, menu_row_height = 48;
};
struct Input {
    Point mouse;
    float wheel_y = 0, wheel_x = 0, seconds = 0;
    bool left_pressed = false, left_down = false;
    bool right_pressed = false, escape = false;
    bool ctrl = false, shift = false;
};
struct RowFrame { ItemId id; Rect bounds; std::vector<Cell> cells; };
struct PanelFrame {
    std::string title;
    Rect bounds, header, body, horizontal_track, horizontal_thumb, vertical_track, vertical_thumb;
    Rect footer; // Game-owned region; no built-in behavior.
    std::vector<Column> columns;
    std::vector<RowFrame> rows; // Only rows intersecting the viewport.
    std::size_t total_rows = 0;
    float scroll_x = 0, scroll_y = 0, max_x = 0, max_y = 0;
    std::optional<ItemId> selected, hovered;
    std::vector<ItemId> selected_ids; // Selected items in the current display order.
};
struct TooltipFrame { Rect bounds; Tooltip content; float row_height = 44; };
struct MenuEntry { Rect bounds; ActionInfo action; std::string label; };
struct MenuFrame {
    Rect bounds;
    std::vector<MenuEntry> entries;
    bool more_above = false, more_below = false;
};
struct Frame {
    std::vector<PanelFrame> panels;
    Rect shared_footer; // Full-width game-owned region below both panels (or one panel).
    std::optional<TooltipFrame> tooltip;
    std::optional<MenuFrame> menu;
    bool captures_pointer = false;
};
struct ActionEvent { std::size_t panel; ItemId item; std::string action; ActionResult result; };

// No window, graphics API, asset loading, or game loop ownership.
class View {
public:
    explicit View(Metrics metrics = {});
    void set_panels(std::vector<Panel> panels); // Zero, one, or two; resets UI state.
    void set_shared_footer_height(float height); // Zero disables the shared region.
    const Frame& update(Rect bounds, const Input& input, const Context& context = {});
    const Frame& frame() const noexcept { return frame_; }
    const std::optional<ActionEvent>& last_action() const noexcept { return last_action_; }
    const std::vector<ActionEvent>& last_actions() const noexcept { return last_actions_; }

private:
    struct State {
        float x = 0, y = 0;
        std::set<ItemId> selected;
        std::optional<ItemId> anchor, primary;
        std::vector<ItemId> order;
    };
    struct Target { std::size_t panel; ItemId item; };
    struct Drag { std::size_t panel; bool horizontal; float offset; };
    Metrics metrics_;
    float shared_footer_height_ = 0;
    std::vector<Panel> panels_;
    std::vector<State> states_;
    Frame frame_;
    std::optional<Target> hover_, menu_;
    std::optional<Drag> drag_;
    Point menu_anchor_;
    float hover_time_ = 0;
    std::size_t menu_offset_ = 0;
    std::optional<ActionEvent> last_action_;
    std::vector<ActionEvent> last_actions_;
    void layout(Rect bounds, const Context& context);
    void layout_menu(Rect bounds, const Context& context);
};

} // namespace game_storage::ui
