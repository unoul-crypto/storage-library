#include <game_storage/ui.hpp>
#include <algorithm>
#include <iostream>
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
}
int main() {
    try { interaction(); menus(); ordering_and_validation(); }
    catch (const std::exception& error) { std::cerr << "Check " << checks << ": " << error.what() << '\n'; return 1; }
    std::cout << "Passed " << checks << " UI checks\n";
}
