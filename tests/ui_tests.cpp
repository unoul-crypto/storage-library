#include <game_storage/ui.hpp>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace game_storage;
namespace gui = game_storage::ui;
namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
gui::Point center(gui::Rect r) { return {r.x + r.width / 2, r.y + r.height / 2}; }
gui::TableConfig config() {
    gui::TableConfig table;
    table.columns = {{"Custom", 320, [](const Item&, const Storage&, const Context& context) {
        return gui::Cell{context.at("label").as<std::string>(), "icon.png"};
    }}, {"Other", 240, {}}};
    table.tooltip = [](const Item&, const Storage&, const Context& context) {
        return gui::Tooltip{{context.at("label").as<std::string>(), "tip.png"}};
    };
    return table;
}
void interaction() {
    Storage a, b;
    for (int i = 0; i < 30; ++i) { a.add(Item()); b.add(Item()); }
    gui::View view;
    view.set_panels({{&a, "A", config()}, {&b, "B", config()}});
    const gui::Rect bounds{10, 20, 800, 340};
    const Context context{{"label", "External content"}};
    gui::Input input;
    auto frame = view.update(bounds, input, context);
    check(frame.panels.size() == 2, "Two panels");
    check(frame.panels[0].bounds.x + frame.panels[0].bounds.width < frame.panels[1].bounds.x, "Panels side by side");
    check(frame.panels[0].rows[0].cells[0].text == "External content", "Cell callback receives context");
    check(frame.panels[0].rows[0].cells[0].png == "icon.png", "Cell supports PNG");
    check(frame.panels[0].rows.size() < 30, "Only visible rows are presented");
    input.mouse = center(frame.panels[0].body); input.wheel_y = -2;
    frame = view.update(bounds, input, context);
    check(frame.panels[0].scroll_y == 88, "Wheel scrolls vertically");
    check(frame.panels[0].scroll_x == 0, "Vertical wheel does not scroll horizontally");
    check(frame.panels[1].scroll_y == 0, "Second table scroll is independent");
    input = {}; input.mouse = center(frame.panels[0].horizontal_thumb); input.left_pressed = input.left_down = true;
    frame = view.update(bounds, input, context);
    input.left_pressed = false; input.mouse.x += 80;
    frame = view.update(bounds, input, context);
    check(frame.panels[0].scroll_x > 0, "Horizontal thumb drags");
    check(frame.panels[0].scroll_y == 88, "Horizontal drag preserves vertical offset");
    check(frame.panels[1].scroll_x == 0, "Horizontal offset is independent");
    input = {}; view.update(bounds, input, context);
    input.mouse = center(frame.panels[0].vertical_thumb); input.left_pressed = input.left_down = true;
    view.update(bounds, input, context);
    input.left_pressed = false; input.mouse.y += 70;
    frame = view.update(bounds, input, context);
    check(frame.panels[0].scroll_y > 88, "Vertical thumb drags");
    input = {}; input.mouse = center(frame.panels[1].rows[0].bounds);
    frame = view.update(bounds, input, context);
    check(!frame.tooltip, "Tooltip waits for delay");
    input.seconds = 0.5f;
    frame = view.update(bounds, input, context);
    check(frame.tooltip && frame.tooltip->content[0].png == "tip.png", "Hover shows PNG tooltip");
    check(frame.tooltip->content[0].text == "External content", "Tooltip uses context");
    check(frame.tooltip->bounds.x + frame.tooltip->bounds.width <= bounds.x + bounds.width, "Tooltip fits right edge");
    input.mouse = {0, 0}; frame = view.update(bounds, input, context);
    check(!frame.tooltip, "Leaving row dismisses tooltip");
    for (const auto& item : a.items()) a.extract(item.id());
    frame = view.update(bounds, input, context);
    check(frame.panels[0].scroll_y == 0 && frame.panels[0].rows.empty(), "Shrinking storage clamps scrolling");
    frame = view.update({0, 0, 1, 1}, input, context);
    check(frame.panels[0].body.height >= 0 && frame.panels[0].body.width >= 0, "Tiny viewport stays valid");
}
void menus() {
    Storage storage;
    Item item; storage.add(item);
    bool allowed = true;
    int calls = 0;
    storage.set_action_provider([&](const Item&, const Storage&, const Context&) {
        return std::vector<Action>{{{"use", allowed, "Blocked"},
            [&](Storage& target, ItemId id, const Context&) { ++calls; target.extract(id); }}};
    });
    gui::View view;
    auto table = config();
    table.action_label = [](const ActionInfo&, const Context&) { return "Use item"; };
    view.set_panels({{&storage, "Items", table}});
    const gui::Rect bounds{0, 0, 500, 300};
    const Context context{{"label", "Text"}};
    gui::Input input;
    auto frame = view.update(bounds, input, context);
    input.mouse = center(frame.panels[0].rows[0].bounds); input.right_pressed = true;
    frame = view.update(bounds, input, context);
    check(frame.menu && frame.menu->entries[0].label == "Use item", "Right click opens labeled action menu");
    check(!frame.tooltip, "Menu suppresses hover tooltip");
    allowed = false;
    input = {}; input.mouse = center(frame.menu->entries[0].bounds); input.left_pressed = true;
    frame = view.update(bounds, input, context);
    check(calls == 0 && view.last_action()->result.status == ActionStatus::disabled, "Action revalidates external state");
    check(frame.menu && frame.menu->entries[0].action.disabled_reason == "Blocked", "Disabled reason stays visible");
    input = {}; input.escape = true;
    frame = view.update(bounds, input, context);
    check(!frame.menu, "Escape closes menu");
    input = {}; input.mouse = center(frame.panels[0].rows[0].bounds); input.right_pressed = true;
    view.update(bounds, input, context);
    input = {}; input.mouse = {490, 290}; input.left_pressed = true;
    frame = view.update(bounds, input, context);
    check(!frame.menu && calls == 0, "Outside click only dismisses menu");
    input = {}; input.mouse = center(frame.panels[0].rows[0].bounds); input.right_pressed = true;
    frame = view.update(bounds, input, context);
    allowed = true;
    input = {}; input.mouse = center(frame.menu->entries[0].bounds); input.left_pressed = true;
    frame = view.update(bounds, input, context);
    check(calls == 1 && storage.size() == 0, "Menu executes developer handler");
    check(!frame.menu && frame.panels[0].rows.empty(), "Mutation refreshes table and closes menu");
    storage.add(item);
    storage.set_action_provider([](const Item&, const Storage&, const Context&) {
        std::vector<Action> result;
        for (int i = 0; i < 20; ++i) result.push_back({{std::to_string(i), false, "Disabled"}, {}});
        return result;
    });
    input = {}; frame = view.update(bounds, input, context);
    input.mouse = center(frame.panels[0].rows[0].bounds); input.right_pressed = true;
    frame = view.update(bounds, input, context);
    check(frame.menu->more_below, "Long menus indicate overflow");
    input = {}; input.mouse = center(frame.menu->bounds); input.wheel_y = -50;
    frame = view.update(bounds, input, context);
    check(frame.menu->entries.back().action.id == "19", "Long menus scroll to last action");
    storage.extract(item.id()); input = {};
    frame = view.update(bounds, input, context);
    check(!frame.menu, "Removed target closes menu");
}
void ordering_and_validation() {
    Storage storage;
    Item a, b; storage.add(a); storage.add(b);
    auto table = config();
    table.order = [](std::vector<Item> items, const Context&) {
        std::reverse(items.begin(), items.end());
        items.push_back(items.front()); items.push_back(Item());
        return items;
    };
    gui::View view;
    view.set_panels({{&storage, "Items", table}});
    const Context context{{"label", "Text"}};
    const auto frame = view.update({0, 0, 500, 300}, {}, context);
    check(frame.panels[0].rows[0].id == b.id(), "Developer controls order");
    check(frame.panels[0].total_rows == 2, "Duplicate and foreign IDs ignored");
    check(storage.items()[0].id() == a.id(), "View ordering never changes storage");
    bool threw = false;
    try { view.set_panels({{nullptr, "", {}}}); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "Null storage rejected");
    threw = false;
    try { view.set_panels({{&storage, "", {}}, {&storage, "", {}}, {&storage, "", {}}}); }
    catch (const std::invalid_argument&) { threw = true; }
    check(threw, "More than two panels rejected");
    view.set_panels({});
    check(view.update({0, 0, 0, 0}, {}).panels.empty(), "View can close all panels");
    check(!view.update({0, 0, 500, 300}, {}).captures_pointer, "Closed view does not capture game input");
}
void multiple_selection_and_actions() {
    Storage storage, other;
    for (int i = 0; i < 6; ++i) storage.add(Item({{"eligible", i % 2 == 0}, {"index", i}}));
    other.add(Item());
    const auto original = storage.items();
    storage.set_action_provider([](const Item& item, const Storage&, const Context&) {
        const bool eligible = item.parameter("eligible") == std::optional<Value>{true};
        std::vector<Action> actions{{{"mark", eligible, "Unavailable"},
            [](Storage& owner, ItemId id, const Context&) { owner.set_item_parameter(id, "marked", true); }}};
        if (item.parameter("index") == std::optional<Value>{2})
            actions.push_back({{"special", true, {}}, [](Storage&, ItemId, const Context&) {}});
        return actions;
    });
    auto table = config();
    table.order = [](std::vector<Item> items, const Context&) {
        std::reverse(items.begin(), items.end());
        return items;
    };
    gui::View view;
    view.set_panels({{&storage, "A", table}, {&other, "B", config()}});
    const gui::Rect bounds{0, 0, 1000, 500};
    const Context context{{"label", "Text"}};
    auto frame = view.update(bounds, {}, context);
    const auto ids = frame.panels[0].rows;
    check(ids[0].id == original[5].id(), "Test display order is sorted in reverse");
    gui::Input input;
    input.mouse = center(ids[1].bounds); input.left_pressed = true;
    frame = view.update(bounds, input, context);
    check(frame.panels[0].selected_ids == std::vector<ItemId>{ids[1].id}, "Normal click selects one item");
    input = {}; input.mouse = center(ids[4].bounds); input.left_pressed = input.ctrl = true;
    frame = view.update(bounds, input, context);
    check(frame.panels[0].selected_ids == (std::vector<ItemId>{ids[1].id, ids[4].id}), "Ctrl adds without clearing selection");
    input = {}; input.mouse = center(ids[2].bounds); input.left_pressed = input.shift = true;
    frame = view.update(bounds, input, context);
    const std::vector<ItemId> selected{ids[1].id, ids[2].id, ids[3].id, ids[4].id};
    check(frame.panels[0].selected_ids == selected, "Shift adds sorted inclusive range to existing selection");
    check(frame.panels[0].selected == ids[2].id, "Primary selection follows last click");
    input = {}; input.mouse = center(frame.panels[1].rows[0].bounds); input.left_pressed = input.ctrl = true;
    frame = view.update(bounds, input, context);
    check(frame.panels[1].selected_ids.size() == 1 && frame.panels[0].selected_ids == selected,
          "Selection is independent for adjacent storages");

    // Right-click an ineligible selected item: the menu uses the group's union.
    input = {}; input.mouse = center(ids[4].bounds); input.right_pressed = true;
    frame = view.update(bounds, input, context);
    check(frame.menu.has_value() && frame.panels[0].selected_ids == selected,
          "Right click on selected item preserves group");
    const auto mark = std::find_if(frame.menu->entries.begin(), frame.menu->entries.end(),
        [](const gui::MenuEntry& entry) { return entry.action.id == "mark"; });
    check(mark != frame.menu->entries.end() && mark->action.enabled,
          "Action enabled when at least one selected item allows it");
    check(std::any_of(frame.menu->entries.begin(), frame.menu->entries.end(),
        [](const gui::MenuEntry& entry) { return entry.action.id == "special"; }),
          "Menu includes actions available on only part of the group");
    input = {}; input.mouse = center(mark->bounds); input.left_pressed = true;
    frame = view.update(bounds, input, context);
    check(!frame.menu, "Successful batch action closes menu");
    check(view.last_actions().size() == 4, "Batch records a result for every selected item");
    for (std::size_t i = 0; i < selected.size(); ++i) {
        check(view.last_actions()[i].item == selected[i], "Batch uses sorted display order");
        const auto expected = (i == 0 || i == 2) ? ActionStatus::executed : ActionStatus::disabled;
        check(view.last_actions()[i].result.status == expected, "Batch skips ineligible items");
        check(storage.item(selected[i])->parameter("marked").has_value() == (expected == ActionStatus::executed),
              "Only eligible items changed");
    }
    check(view.last_action()->item == selected.back(), "Last action compatibility accessor remains available");
    input = {}; frame = view.update(bounds, input, context);
    check(view.last_actions().empty() && !view.last_action(), "Batch results reset on next update");
    input.mouse = center(ids[2].bounds); input.right_pressed = true;
    frame = view.update(bounds, input, context);
    const auto special = std::find_if(frame.menu->entries.begin(), frame.menu->entries.end(),
        [](const gui::MenuEntry& entry) { return entry.action.id == "special"; });
    check(special != frame.menu->entries.end(), "Partial-group action stays in menu");
    input = {}; input.mouse = center(special->bounds); input.left_pressed = true;
    view.update(bounds, input, context);
    check(view.last_actions().size() == 4, "Partial-group action reports each selected item");
    for (std::size_t i = 0; i < selected.size(); ++i)
        check(view.last_actions()[i].result.status == (i == 2 ? ActionStatus::executed : ActionStatus::action_not_found),
              "Action absent on an item is skipped");
    check(other.size() == 1, "Batch action does not affect second storage");
    frame = view.update(bounds, {}, context);
    input = {};
    input.mouse = center(ids[5].bounds); input.right_pressed = true;
    frame = view.update(bounds, input, context);
    check(frame.panels[0].selected_ids == std::vector<ItemId>{ids[5].id},
          "Right click on unselected item starts a new group");
    input = {}; input.escape = true;
    view.update(bounds, input, context);
    input = {}; input.mouse = center(ids[0].bounds); input.left_pressed = true;
    frame = view.update(bounds, input, context);
    check(frame.panels[0].selected_ids == std::vector<ItemId>{ids[0].id}, "Ordinary click clears previous group");
}
void range_across_scrolled_rows() {
    Storage storage;
    for (int i = 0; i < 16; ++i) storage.add(Item({{"index", i}}));
    auto table = config();
    table.order = [](std::vector<Item> items, const Context&) {
        std::reverse(items.begin(), items.end()); return items;
    };
    gui::View view;
    view.set_panels({{&storage, "Items", table}});
    const gui::Rect bounds{0, 0, 500, 300};
    const Context context{{"label", "Text"}};
    auto frame = view.update(bounds, {}, context);
    const auto anchor = frame.panels[0].rows[0].id;
    gui::Input input;
    input.mouse = center(frame.panels[0].rows[0].bounds); input.left_pressed = true;
    view.update(bounds, input, context);
    input = {}; input.mouse = center(frame.panels[0].body); input.wheel_y = -10;
    frame = view.update(bounds, input, context);
    check(frame.panels[0].rows.front().id != anchor, "Anchor is offscreen after scrolling");
    const auto target = frame.panels[0].rows.front().id;
    input = {}; input.mouse = center(frame.panels[0].rows.front().bounds);
    input.left_pressed = input.shift = true;
    frame = view.update(bounds, input, context);
    check(frame.panels[0].selected_ids.front() == anchor && frame.panels[0].selected_ids.back() == target,
          "Shift range follows full sorted list across viewport");
    const auto expected_size = frame.panels[0].selected_ids.size();
    check(expected_size > 5, "Offscreen items are included in range");
    storage.extract(anchor);
    input = {}; frame = view.update(bounds, input, context);
    check(frame.panels[0].selected_ids.size() == expected_size - 1,
          "Removed items leave selection automatically");
}
void custom_regions() {
    Storage a, b;
    for (int i = 0; i < 12; ++i) a.add(Item());
    gui::View view;
    view.set_panels({{&a, "A", config(), 45}, {&b, "B", config(), 70}});
    view.set_shared_footer_height(35);
    const gui::Rect bounds{10, 20, 800, 320};
    const Context context{{"label", "Text"}};
    auto frame = view.update(bounds, {}, context);
    check(frame.shared_footer.y == 305 && frame.shared_footer.height == 35,
          "Shared footer reserves bottom space across both panels");
    check(frame.panels[0].footer.height == 45 && frame.panels[1].footer.height == 70,
          "Each panel has its own footer height");
    check(frame.panels[0].body.y + frame.panels[0].body.height <= frame.panels[0].footer.y,
          "Table body does not overlap custom footer");
    check(frame.panels[0].footer.y + frame.panels[0].footer.height == frame.shared_footer.y,
          "Panel footer ends at shared footer");
    gui::Input input;
    input.mouse = center(frame.panels[0].footer);
    input.left_pressed = true; input.wheel_y = -2;
    frame = view.update(bounds, input, context);
    check(frame.captures_pointer && frame.panels[0].scroll_y == 0 && frame.panels[0].selected_ids.empty(),
          "Custom panel input is captured without selecting or scrolling the table");
    input = {}; input.mouse = center(frame.shared_footer); input.right_pressed = true;
    frame = view.update(bounds, input, context);
    check(frame.captures_pointer && !frame.menu, "Shared region captures input without opening a table menu");
    frame = view.update({10, 20, 800, 25}, {}, context);
    check(frame.shared_footer.height == 25 && frame.panels[0].footer.height == 0 && frame.panels[0].body.height == 0,
          "Custom regions clamp safely in a tiny viewport");
    bool threw = false;
    try { view.set_shared_footer_height(-1); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "Negative shared height rejected");
    threw = false;
    try { view.set_panels({{&a, "A", config(), std::numeric_limits<float>::infinity()}}); }
    catch (const std::invalid_argument&) { threw = true; }
    check(threw, "Nonfinite panel height rejected");
    view.set_panels({{&a, "A", config(), 45}});
    frame = view.update(bounds, {}, context);
    check(frame.panels.size() == 1 && frame.panels[0].footer.width == bounds.width &&
          frame.shared_footer.width == bounds.width,
          "Single storage can use both custom regions at full width");
    view.set_panels({});
    frame = view.update(bounds, {}, context);
    check(frame.shared_footer.height == 0 && !frame.captures_pointer,
          "Closed view has no shared region or pointer capture");
}
}
int main() {
    try { interaction(); menus(); ordering_and_validation(); multiple_selection_and_actions(); range_across_scrolled_rows(); custom_regions(); }
    catch (const std::exception& error) { std::cerr << "Check " << checks << ": " << error.what() << '\n'; return 1; }
    std::cout << "Passed " << checks << " UI checks\n";
}
