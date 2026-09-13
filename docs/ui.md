# Optional storage tables

Three independent CMake targets are available:

| Target | Role | Dependencies |
| --- | --- | --- |
| `game_storage::game_storage` | Storage and actions | C++17 standard library |
| `game_storage::ui` | Layout, scrolling, selection, tooltips, action menus | Core |
| `game_storage::raylib` | Text, PNG textures, colors, mouse/keyboard adapter | UI and raylib |

`GAME_STORAGE_BUILD_UI` defaults to ON; `GAME_STORAGE_BUILD_RAYLIB` defaults to OFF.
Set both OFF to build only the core. No dependency is downloaded automatically.
The UI module can be used with a custom renderer by consuming `ui::Frame`.

## Build the graphical example

Supply an existing raylib source checkout (tested with raylib 6.0), or omit the
source option and provide an installed raylib CMake package via `CMAKE_PREFIX_PATH`.
If the host already defines a `raylib` target, it is reused.
The tested raylib checkout requires CMake 3.25+; the core and UI behavior require
3.21+. Quote source paths containing spaces.

```sh
cmake -S . -B build/ui-desktop -DGAME_STORAGE_BUILD_RAYLIB=ON -DGAME_STORAGE_RAYLIB_SOURCE_DIR=/path/to/raylib
cmake --build build/ui-desktop --config Release
ctest --test-dir build/ui-desktop -C Release --output-on-failure
```

Run `Release/storage_ui_example.exe` from **build/ui-desktop** on MSVC (or
`./storage_ui_example` with a single-configuration generator). The working directory
must contain the copied `assets` directory. The example owns its window and loop;
neither library module creates a window. `--smoke` renders a few hidden frames,
shows Ctrl/Shift selection in `storage-ui.png`, and exits on desktop.

For example, after building with MSVC, run these PowerShell commands from the
repository root:

```powershell
Push-Location build/ui-desktop
try { & ./Release/storage_ui_example.exe } finally { Pop-Location }
```

See [the complete demo](../examples/table.cpp) for custom prices, sorting, PNGs,
tooltips, capacity footers, a shared transfer button, and core action handlers.
Its [browser shell](../examples/web_shell.html)
fills the browser viewport and suppresses the canvas context menu.

With an activated Emscripten environment and Ninja:

```sh
emcmake cmake -S . -B build/ui-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release -DPLATFORM=Web -DGAME_STORAGE_BUILD_RAYLIB=ON -DGAME_STORAGE_RAYLIB_SOURCE_DIR=/path/to/raylib
cmake --build build/ui-wasm
ctest --test-dir build/ui-wasm --output-on-failure
python -m http.server 8080 --directory build/ui-wasm
```

Open `http://localhost:8080/storage_ui_example.html`. PNGs are preloaded into the
Emscripten filesystem at `/assets`. Deploy the HTML, JS, WASM, and DATA files together.
The UI/core tests run in Node without a graphics context.

For a subdirectory consumer, enable the desired options before `add_subdirectory`
and link `game_storage::raylib`. Installed package users request components:

```cmake
find_package(game_storage CONFIG REQUIRED COMPONENTS raylib)
target_link_libraries(my_game PRIVATE game_storage::raylib)
```

Install raylib separately when building it from a source checkout: our installation
does not bundle its dependencies. `find_package(game_storage CONFIG REQUIRED)`
loads the core and available UI behavior without looking for raylib. The optional
`ui` component can require that UI behavior was built.

## Define a table

```cpp
#include <game_storage/raylib_renderer.hpp>
using namespace game_storage;
namespace gui = game_storage::ui;

Storage chest, bag;
chest.add(Item(Parameters{{"name", "Health potion"}}, Parameters{{"quantity", 1}}));
Context context{{"display_price", "25 G"}};

gui::TableConfig table;
table.columns = {
    {"Item", 240, [](const Item& item, const Storage&, const Context&) {
        return gui::Cell{item.item_data_value("name")->as<std::string>(), "assets/potion.png"};
    }},
    {"Quantity", 100, [](const Item& item, const Storage&, const Context&) {
        return gui::Cell{std::to_string(item.entry_property("quantity")->as<std::int64_t>()), {}};
    }},
    {"Price", 120, [](const Item&, const Storage&, const Context& context) {
        return gui::Cell{context.at("display_price").as<std::string>(), {}};
    }}
};
table.tooltip = [](const Item&, const Storage&, const Context&) {
    return gui::Tooltip{{"Custom description", "assets/potion.png"}, {"Another line", {}}};
};

gui::View view;
view.set_panels({{&chest, "Chest", table}, {&bag, "Bag", table}});
```

This snippet uses the example asset directory. Its item schema is application-owned:
the `name` item-data value, `quantity` entry property, and `display_price` context
value must exist with the indicated types. Construct such an entry with
`Item(Parameters{{"name", "Health potion"}}, Parameters{{"quantity", 1}})`.

To supply sorting, set `table.order` **before** passing the configuration to
`set_panels` (include `<algorithm>` for this example):

```cpp
table.order = [](std::vector<Item> items, const Context&) {
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.item_data_value("name")->as<std::string>() < b.item_data_value("name")->as<std::string>();
    });
    return items;
};
```

Pass one panel for a full-width table or two for equal-width tables side by side.
`set_panels({})` closes everything. `set_panels` resets selection, offsets, and
popups. Storage pointers are borrowed and must stay valid; configurations and
callbacks are copied. Content/order/tooltip/label callbacks must be pure and must
not mutate the view or storages during an update. They may run more than once per
frame. Handlers registered on the core storage may change its contents.

`Cell` supports text, a PNG key, or both. Empty content is allowed. Column titles
and positive widths are developer-defined. `TableConfig::order` receives snapshot
items and context and returns the desired order (optionally filtered). There is no
built-in header sorting. Foreign and duplicate IDs are ignored; changed snapshot
data and entry properties are not written back. Sorting never modifies the core
storage list.

Callbacks can derive content from external systems via context or captured state;
display values do not have to exist in the item dictionary. Exceptions propagate
to the game. Only visible rows run cell callbacks, but ordering receives the full
list each update. This version favors simple synchronous snapshots over caching.

## Add game-owned UI inside the view

Set `Panel::footer_height` for each table and `View::set_shared_footer_height` for
an optional full-width region beneath the panels. Heights are screen-coordinate
units, must be finite and nonnegative, and are clamped to the available view height.
Both default to zero. With two panels, each table still sits beside the other;
the shared footer spans the complete view. `PanelFrame::footer` and
`Frame::shared_footer` expose the allocated rectangles, including after a resize.
The view reserves space but does not define their content or game rules.

```cpp
view.set_panels({{&chest, "Chest", table, 48}, {&bag, "Bag", table, 48}});
view.set_shared_footer_height(52);
renderer.set_custom_drawer([&](std::optional<std::size_t> panel, gui::Rect area) {
    if (panel) {
        const Storage& owner = *(*panel == 0 ? &chest : &bag);
        // Draw owner.size(), capacity from owner.parameter("capacity"), etc.
    } else {
        // Draw a shared button, status area, or other game-specific control.
    }
});
```

The raylib callback runs once for every nonempty panel footer and once for the
nonempty shared footer. `panel` is the zero-based panel index; `std::nullopt` means
the shared footer. Drawing is clipped to the supplied rectangle and occurs after
the tables but before tooltips and action menus. The callback uses the game's
raylib drawing calls and must not open its own scissor mode. The game owns any
captured data and textures, which must remain valid during drawing.

Handle pointer input in the game loop using the same frame and input passed to the
view. For example, after `view.update(...)`, check
`input.left_pressed && frame.shared_footer.contains(input.mouse)` and then test
the game's button rectangle within that region. The view captures pointer input
inside either footer, but it does not select table rows, open item menus, or scroll
the table there. The game defines what a control does and may refresh the view
after changing storage. See the [example](../examples/table.cpp) for a working
capacity panel and a button that transfers selected items.

Other renderers can draw directly from these rectangles and route input the same
way; no raylib types enter the core or `ui::View`. The reserved region is an
extension point for arbitrary game UI, including nested controls managed entirely
by the game.

## Integrate with a game loop

Create `RaylibRenderer` after initializing raylib and destroy it before
`CloseWindow()`. The game owns window size, scaling, asset paths, and the loop:

```cpp
gui::RaylibRenderer renderer; // After InitWindow(); keep alive across frames.
```

Inside each frame:

```cpp
auto input = gui::RaylibRenderer::poll_input();
const auto& frame = view.update({20, 80, 1000, 500}, input, context);
// Route pointer input to the game only if !frame.captures_pointer.
BeginDrawing();
ClearBackground(BLACK);
renderer.draw(frame, input.mouse);
EndDrawing();
```

Call drawing in ordinary 2D screen coordinates, outside a camera or active scissor
block. The renderer sets and clears scissor regions; it does not preserve an outer
scissor. Pass matching coordinates for the bounds and pointer if adapting input.
The host should handle browser context-menu suppression if using a custom shell
(the included example shell suppresses the canvas context menu).

`Frame` is valid until the next update. UI state lives in the `View`, never in the
storage. Multiple independent `View` objects are allowed. Like the core, updates
are single-threaded and synchronous. Avoid reentrant updates from callbacks.

## Interactions and customization

- Each table has independent horizontal and vertical offsets and draggable thumbs.
  Clicking a track moves its thumb toward the click. Offsets clamp when rows or the
  viewport change. Both tracks reserve space; a thumb appears only for overflow.
- Normal wheel input affects the hovered table vertically. Horizontal wheel/trackpad
  input affects it horizontally; the bottom thumb also works with an ordinary mouse.
- Left click selects a row by item ID. Hover opens a developer-supplied tooltip after
  `Metrics::tooltip_delay`. Moving to another row, scrolling, or leaving resets it.
- Ctrl+left click adds a row to the selection; Shift+left click adds the inclusive
  range from the last selection anchor through the clicked row, using the current
  developer-defined display order. Ctrl+Shift also adds that range. A plain left
  click starts a new selection. Each of two tables keeps its own selection.
  `PanelFrame::selected_ids` exposes all selected IDs in display order;
  `PanelFrame::selected` is the primary selection, normally the last clicked ID.
- Right click opens that item's action menu. Labels use `TableConfig::action_label`
  or the core action ID. Right-clicking a selected row keeps the group; right-clicking
  an unselected row starts a new selection. The menu combines actions from all
  selected items in that table. An action is enabled if any selected item enables
  it. Disabled reasons are shown. Empty catalogs show `No actions`.
- The current catalog is refreshed while the menu is open, and the core rechecks it
  on execution for each selected item, in display order. Items for which an action
  is missing or disabled are skipped without invoking a handler. Pass current
  context every frame. Inspect `view.last_actions()` during the same frame to see
  each item's `ActionResult` (including skipped statuses); the vector clears at the
  next update. `last_action()` remains available as the final per-item result.
  The batch runs sequentially and is not transactional: if a handler throws,
  changes made by earlier handlers remain in storage.
- Escape or an outside click dismisses the menu without clicking through. Disabled
  actions leave it open; other results close it. Menus with many actions scroll by
  wheel, with colored edge indicators for additional entries.
- No drag-and-drop transfers are built in. Add a core action handler that performs
  the game's desired transfer if needed.

Change row/header sizes, panel gap, tooltip delay/width and menu dimensions through
`Metrics` at construction. `RaylibRenderer::theme()` exposes colors, font, padding,
font size and image size. A custom font is borrowed; the game loads the desired
glyphs and owns its lifetime. Default display strings are English.

PNG paths are loaded lazily and cached by the default renderer. Missing images get
a placeholder and are not retried until `clear_images()`. A supplied `ImageResolver`
can map asset keys to existing game textures; those textures are borrowed and never
unloaded by the renderer. Preserve them until drawing completes. Example icons are
original procedural pixel-art PNGs included in `examples/assets`.

Cells and tooltip rows have fixed heights; text supports explicit newlines and
long lines are ellipsized. Set suitable row heights and column widths for your
content. Tooltips clamp to the view rectangle; content taller than that rectangle
is clipped, so keep hover descriptions concise. Popups stay within the supplied
view bounds. This first version uses mouse interaction; keyboard navigation and
touch gestures are not implemented.

## Replacing the renderer

Include `game_storage/ui.hpp` and link `game_storage::ui` to use the same table
behavior in another engine. Convert the game's pointer position, button states,
wheel deltas, Escape key, and frame duration to `ui::Input`, then call `View::update`.
Draw the resulting `Frame` using the game's own graphics API:

- `PanelFrame` provides titles, columns, visible rows, scroll offsets, clipping
  rectangles, footer bounds, scrollbar tracks/thumbs, and selected/hovered item IDs. Highlight
  every ID in `selected_ids` when rendering multi-selection.
- Start column drawing at `body.x - scroll_x`; row bounds already include vertical
  scrolling. Clip row content to `body` and headings to `header`.
- Draw the optional tooltip and menu after the panels, using their supplied bounds.
  Draw `Frame::shared_footer` below the panels if enabled.
  Resolve each `Cell::png` asset key through the game's own texture system.
- `captures_pointer` tells the host whether to withhold pointer input from the
  world. The view executes menu actions through the core; a renderer only draws.

Use the [raylib implementation](../src/raylib_renderer.cpp) as a reference or replace
both UI modules and call the core directly. No graphics types appear in the core
or renderer-independent UI headers.
