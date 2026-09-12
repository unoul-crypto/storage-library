#include <game_storage/raylib_renderer.hpp>
#include <algorithm>
#include <memory>
#include <string>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

using namespace game_storage;
namespace gui = game_storage::ui;

gui::Rect move_button(gui::Rect footer) {
    return {footer.x + footer.width - 206, footer.y + 9, 192, std::max(0.0f, footer.height - 18)};
}

struct Demo {
    Storage chest, bag;
    gui::View view;
    gui::RaylibRenderer renderer;
    bool smoke = false;
    int frames = 0;

    explicit Demo(bool test) : smoke(test) {
        chest.set_parameter("capacity", 40);
        bag.set_parameter("capacity", 20);
        for (int i = 0; i < 35; ++i) {
            chest.add(Item({{"name", std::string(i % 2 ? "Moonstone" : "Health potion")},
                            {"kind", i % 2 ? "Gemstone" : "Consumable"}, {"value", 10 + i * 3},
                            {"png", i % 2 ? "assets/gem.png" : "assets/potion.png"}}));
        }
        for (int i = 0; i < 12; ++i)
            bag.add(Item({{"name", "Moonstone"}, {"kind", "Gemstone"}, {"value", 24 + i}, {"png", "assets/gem.png"}}));
        auto provider = [](const Item&, const Storage&, const Context&) {
            return std::vector<Action>{
                {{"inspect", true, {}}, [](Storage& storage, ItemId id, const Context&) {
                    storage.set_item_parameter(id, "inspected", true);
                }},
                {{"sell", false, "Visit a merchant to sell"}, {}},
                {{"discard", true, {}}, [](Storage& storage, ItemId id, const Context&) { storage.extract(id); }}
            };
        };
        chest.set_action_provider(provider); bag.set_action_provider(provider);
        gui::TableConfig table;
        table.columns = {
            {"ITEM", 230, [](const Item& item, const Storage&, const Context&) {
                return gui::Cell{item.parameter("name")->as<std::string>(), item.parameter("png")->as<std::string>()};
            }},
            {"CATEGORY", 170, [](const Item& item, const Storage&, const Context&) {
                return gui::Cell{item.parameter("kind")->as<std::string>(), {}};
            }},
            {"VALUE", 100, [](const Item& item, const Storage&, const Context& context) {
                const auto multiplier = context.at("price_multiplier").as<std::int64_t>();
                return gui::Cell{std::to_string(item.parameter("value")->as<std::int64_t>() * multiplier) + " G", {}};
            }},
            {"NOTES", 220, [](const Item& item, const Storage&, const Context&) {
                return gui::Cell{item.parameter("inspected") ? "Inspected" : "Unknown properties", {}};
            }}
        };
        table.order = [](std::vector<Item> items, const Context&) {
            std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
                return a.parameter("name")->as<std::string>() < b.parameter("name")->as<std::string>();
            });
            return items;
        };
        table.tooltip = [](const Item& item, const Storage&, const Context&) {
            return gui::Tooltip{{item.parameter("name")->as<std::string>(), item.parameter("png")->as<std::string>()},
                                {"A discovery from the old ruins.", {}}, {"Right-click for actions", {}}};
        };
        table.action_label = [](const ActionInfo& action, const Context&) {
            if (action.id == "inspect") return std::string("Inspect item");
            if (action.id == "sell") return std::string("Sell to merchant");
            return std::string("Discard item");
        };
        view.set_panels({{&chest, "01 / CHEST", table, 55}, {&bag, "02 / BACKPACK", table, 55}});
        view.set_shared_footer_height(54);
        renderer.set_custom_drawer([this](std::optional<std::size_t> panel, gui::Rect area) {
            if (panel) {
                const Storage& storage = *(*panel == 0 ? &chest : &bag);
                const auto capacity = storage.parameter("capacity")->as<std::int64_t>();
                const float ratio = std::min(1.0f, static_cast<float>(storage.size()) / static_cast<float>(capacity));
                const auto label = std::to_string(storage.size()) + " / " + std::to_string(capacity) + " items";
                DrawText(label.c_str(), static_cast<int>(area.x + 12), static_cast<int>(area.y + 7), 16,
                         {230, 237, 244, 255});
                const float bar_width = std::max(0.0f, area.width - 24);
                DrawRectangle(static_cast<int>(area.x + 12), static_cast<int>(area.y + 34),
                              static_cast<int>(bar_width), 8, {17, 24, 35, 255});
                DrawRectangle(static_cast<int>(area.x + 12), static_cast<int>(area.y + 34),
                              static_cast<int>(bar_width * ratio), 8, {91, 215, 181, 255});
            } else {
                DrawText("CUSTOM AREA / GAME-OWNED CONTROLS", static_cast<int>(area.x + 14),
                         static_cast<int>(area.y + 18), 16, {151, 168, 188, 255});
                const auto button = move_button(area);
                DrawRectangleRec({button.x, button.y, button.width, button.height}, {37, 73, 84, 255});
                DrawText("MOVE SELECTED  >", static_cast<int>(button.x + 12),
                         static_cast<int>(button.y + 9), 16, {230, 237, 244, 255});
            }
        });
    }
    void tick() {
        auto input = gui::RaylibRenderer::poll_input();
        if (smoke) {
            input = {};
            input.mouse = {100, 211.0f + static_cast<float>(frames) * 44.0f};
            input.seconds = 0.6f;
            if (frames < 3) {
                input.left_pressed = true;
                input.ctrl = frames == 1;
                input.shift = frames == 2;
            }
        }
        const gui::Rect area{24, 118, static_cast<float>(GetScreenWidth()) - 48,
                             static_cast<float>(GetScreenHeight()) - 170};
        const Context context{{"price_multiplier", 2}};
        const auto& frame = view.update(area, input, context);
        if (input.left_pressed && frame.shared_footer.contains(input.mouse) &&
            move_button(frame.shared_footer).contains(input.mouse)) {
            const auto capacity = bag.parameter("capacity")->as<std::int64_t>();
            for (ItemId id : frame.panels[0].selected_ids) {
                if (static_cast<std::int64_t>(bag.size()) >= capacity) break;
                chest.transfer_to(id, bag);
            }
            view.update(area, {}, context);
        }
        BeginDrawing();
        ClearBackground({12, 18, 28, 255});
        DrawText("STORAGE / EXPLORER", 24, 24, 28, {230, 237, 244, 255});
        DrawText("Two storages. Your data, presentation and actions.", 24, 66, 18, {151, 168, 188, 255});
        renderer.draw(frame, input.mouse);
        DrawText("Wheel: rows  /  Drag: columns  /  Ctrl: add  /  Shift: range  /  Right-click: actions", 24,
                 GetScreenHeight() - 32, 16, {151, 168, 188, 255});
        EndDrawing();
        ++frames;
        if (smoke && frames == 4) TakeScreenshot("storage-ui.png");
    }
};

#ifdef __EMSCRIPTEN__
std::unique_ptr<Demo> demo;
void web_frame() { demo->tick(); }
#endif
int main(int argc, char** argv) {
    const bool smoke = argc > 1 && std::string(argv[1]) == "--smoke";
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | (smoke ? FLAG_WINDOW_HIDDEN : 0));
    InitWindow(1120, 700, "Storage Library - Table Demo");
    SetExitKey(KEY_NULL); // Escape dismisses menus; the host owns application exit.
    SetTargetFPS(60);
#ifdef __EMSCRIPTEN__
    demo = std::make_unique<Demo>(smoke);
    emscripten_set_main_loop(web_frame, 0, 1);
#else
    {
        Demo app(smoke);
        while (!WindowShouldClose() && (!smoke || app.frames < 5)) app.tick();
    }
    CloseWindow();
#endif
    return 0;
}
