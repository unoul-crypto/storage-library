#include <game_storage/raylib_renderer.hpp>
#include <algorithm>
#include <memory>
#include <string>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

using namespace game_storage;
namespace gui = game_storage::ui;

struct Demo {
    Storage chest, bag;
    gui::View view;
    gui::RaylibRenderer renderer;
    bool smoke = false;
    int frames = 0;

    explicit Demo(bool test) : smoke(test) {
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
        view.set_panels({{&chest, "01 / CHEST", table}, {&bag, "02 / BACKPACK", table}});
    }
    void tick() {
        auto input = gui::RaylibRenderer::poll_input();
        if (smoke) { input.mouse = {100, 205}; input.seconds = 0.6f; }
        const auto& frame = view.update({24, 118, static_cast<float>(GetScreenWidth()) - 48,
                                        static_cast<float>(GetScreenHeight()) - 170}, input, {{"price_multiplier", 2}});
        BeginDrawing();
        ClearBackground({12, 18, 28, 255});
        DrawText("STORAGE / EXPLORER", 24, 24, 28, {230, 237, 244, 255});
        DrawText("Two storages. Your data, presentation and actions.", 24, 66, 18, {151, 168, 188, 255});
        renderer.draw(frame, input.mouse);
        DrawText("Scroll to browse  /  Drag bottom bar for columns  /  Right-click for actions", 24,
                 GetScreenHeight() - 32, 16, {151, 168, 188, 255});
        EndDrawing();
        ++frames;
        if (smoke && frames == 3) TakeScreenshot("storage-ui.png");
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
        while (!WindowShouldClose() && (!smoke || app.frames < 4)) app.tick();
    }
    CloseWindow();
#endif
    return 0;
}
