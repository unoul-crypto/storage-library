# Game Storage

A C++17 storage library for desktop and WebAssembly games, with an optional table
interface. Developers own game rules, item presentation, ordering, and actions.

| Module / CMake target | What it provides |
| --- | --- |
| `game_storage::game_storage` | Items, parameters, extraction, transfers, and actions; no graphics dependencies |
| `game_storage::ui` | Table layout, scrolling, selection, tooltips, and menus; no rendering API |
| `game_storage::raylib` | Optional raylib drawing, PNG loading, and input adapter |

The interface supports text and PNG cells, computed values from external context,
developer-controlled ordering, hover tooltips, right-click action menus, and one or
two tables with independent horizontal and vertical scrolling. Normal mouse-wheel
input scrolls vertically. Ctrl adds items to selection; Shift adds a sorted range.
Actions apply to every selected item where the game permits them. Game rules and
transfers remain under developer control.
Each table can reserve a footer for game-owned controls, and the view can reserve
one shared footer below both tables. The game supplies drawing and input behavior.

- [UI setup, customization, and graphical demo](docs/ui.md)
- [Core usage example](examples/basic.cpp)
- [Two-storage UI example](examples/table.cpp)
- [Core API](include/game_storage/storage.hpp) and [UI API](include/game_storage/ui.hpp)

Item slots, stacks, partial extraction, and drag-and-drop transfers are not implemented
in this version. Storage data can be saved and restored through versioned JSON.

## Build and test

Requires CMake 3.21+ and a C++17 compiler. Run from the repository root:

```sh
cmake -S . -B build/desktop
cmake --build build/desktop --config Release
ctest --test-dir build/desktop -C Release --output-on-failure
```

The default build includes the core and UI behavior, without raylib or a graphical
window. Enable `GAME_STORAGE_BUILD_RAYLIB` for the graphical example described in
[the UI guide](docs/ui.md). Building a raylib source checkout also requires the
CMake version requested by that checkout (3.25+ for the tested raylib 6.0 checkout).

| CMake option | Default | Purpose |
| --- | --- | --- |
| `GAME_STORAGE_BUILD_UI` | `ON` | Build renderer-independent UI behavior |
| `GAME_STORAGE_BUILD_RAYLIB` | `OFF` | Build the renderer; requires UI and raylib |
| `GAME_STORAGE_RAYLIB_SOURCE_DIR` | Empty | Use an existing raylib source directory instead of finding an installed package |
| `GAME_STORAGE_BUILD_TESTS` | `ON` for standalone builds | Build core and enabled UI tests |
| `GAME_STORAGE_BUILD_EXAMPLES` | `ON` for standalone builds | Build the core example and, if enabled, the graphical example |

For the core alone, configure with both `-DGAME_STORAGE_BUILD_UI=OFF` and
`-DGAME_STORAGE_BUILD_RAYLIB=OFF`. No dependencies are downloaded automatically.

On Windows, if CMake is not on PATH, set a PowerShell variable to its executable
and replace `cmake` with `& $cmake` (and use `ctest.exe` from the same directory).
For MSVC you can explicitly select `-G "Visual Studio 17 2022" -A x64` at configure time.

The example is `build/desktop/Release/storage_example.exe` with MSVC, or
`build/desktop/storage_example` with a single-configuration generator.

## Use in a game

```cmake
add_subdirectory(path/to/game_storage)
target_link_libraries(my_game PRIVATE game_storage::game_storage)
```

Tests and examples default to OFF when included as a subdirectory. Alternatively,
install and consume the package:

```sh
cmake --install build/desktop --config Release --prefix install
```

```cmake
find_package(game_storage 0.6 CONFIG REQUIRED)
target_link_libraries(my_game PRIVATE game_storage::game_storage)
```

Pass the installation prefix in `CMAKE_PREFIX_PATH` when configuring the consumer.

```cpp
#include <game_storage/storage.hpp>
using namespace game_storage;

Storage chest({{"label", "Chest"}});
Storage bag;
Item sword(Parameters{{"type", "sword"}, {"durability", 100}},
           Parameters{{"quantity", 1}});

chest.add(sword);
chest.set_item_data_value(sword.id(), "durability", 80);
chest.set_entry_property(sword.id(), "quantity", 2);
auto snapshot = chest.items();
auto result = chest.transfer_to(sword.id(), bag);
auto removed = bag.extract(sword.id()); // optional<Item>
```

## Data and identity

- `Parameters` is a string-keyed dictionary. `Value` owns null, bool, signed 64-bit
  integer, double, string, `Value::Array`, or `Value::Object`. Signed integer
  literals such as `25` become `int64_t`; use an explicitly checked conversion for
  unsigned application values. Use `value.as<T>()` to access a known type, or
  `std::get_if<T>(&value.data)` to inspect safely. A wrong `as<T>()` throws.
- Each entry has an `item_data` dictionary for the item's own data and a separate
  `properties` dictionary for its state in a storage, such as `quantity`. Construct
  it with `Item(item_data, entry_properties)`; the second argument defaults to an
  empty dictionary. These dictionaries may contain nested values and can both be
  changed. `quantity` is ordinary data for now; it does not trigger stacking or
  partial extraction.
- Each new `Item` receives a nonzero `uint64_t` ID. Identity is stored separately
  from both dictionaries and has no setter. A key named `"id"` in either dictionary
  is ordinary application metadata and does not change `item.id()`.
- Copying an item preserves its ID and deep-copies both dictionaries. To create
  another instance of the same item type, construct `Item(existing.item_data())`.
- IDs are unique across new items within one linked library instance during a
  process/module lifetime. JSON snapshots preserve and restore them. They are not
  distributed or cross-module identifiers; loading separate snapshots with
  overlapping IDs is the game's responsibility.
- `add` copies an item. Duplicate IDs are rejected within the receiving storage;
  different storages do not share a global ownership registry. Use `transfer_to`
  for movement instead of adding the same snapshot to multiple storages.
- Items remain in insertion order. Removal preserves the order of remaining items;
  transfer appends to the destination. Lookup is linear, suitable for an initial
  small-to-medium game inventory core. There is no automatic stacking.
- All item/list/parameter reads return independent snapshots, including nested
  arrays and dictionaries. Mutating a snapshot never changes storage. Use
  `set_item_data_value` / `remove_item_data_value` and `set_entry_property` /
  `remove_entry_property` to change a stored entry, or `set_parameter` /
  `remove_parameter` for the storage itself. `Item::item_data_value` and
  `Item::entry_property` read individual values. An unattached `Item` has matching
  setters and removers. The original `Item::parameters` / `parameter` and
  `Storage::set_item_parameter` names remain aliases for `item_data`.
- Missing lookups return `std::nullopt`, distinct from a present `Value(nullptr)`.
  Parameter setters insert or replace. Removal returns false if the item or key
  is absent; setting a parameter on a missing item returns false.
- Storage objects cannot be copied or moved; use owning pointers when dynamic
  ownership is needed. Obtain item snapshots explicitly through `items()`.

## Custom game classes

Include `game_storage/item_adapter.hpp` to use the optional, header-only
`ItemAdapter<T>` with the core target. No inheritance or graphics dependencies
are required. The game provides two callbacks:

- Encoder: `void(const T&, Parameters& item_data, Parameters& properties)`.
  Assign the keys owned by your class; erase obsolete keys explicitly if needed.
- Decoder: `T(const Parameters& item_data, const Parameters& properties)`.
  Validate the game's type/schema and construct an independent object.

`adapter.to_item(value, properties)` creates a new entry with a new ID, starting
with empty item data and the optional supplied properties. Add it with
`storage.add(entry)`. `adapter.from_item(entry)` reconstructs the game object.
`adapter.update(storage, id, value)` updates an existing entry while preserving
its ID, position, and any keys the encoder leaves untouched. Missing IDs return
false without calling the encoder. Both dictionaries are committed together;
an encoder exception leaves the stored entry unchanged. Callbacks must not mutate
the storage or reenter its operations; external callback side effects cannot be
rolled back. Decoder exceptions propagate to the game.

The class object and stored entry are independent; changes require an explicit
`update`. Keep the entry ID separately in the game. JSON remains version 2 and
contains the dictionaries; adapters and C++ type names are not serialized. For
heterogeneous items, the game can store a stable type key and choose its adapter
when reading. Loading JSON itself does not invoke adapters or validate game types.
See [the complete custom-class example](examples/custom_item.cpp), including a
save/load round trip. Build/run `storage_adapter_example` like `storage_example`.

## Technical operations

| Operation | Result |
| --- | --- |
| `add(item)` | `added` or `duplicate_id` |
| `extract(id)` | Whole item or `std::nullopt` |
| `items()` / `item(id)` | List snapshot / optional item snapshot |
| `transfer_to(id, destination)` | `transferred`, `item_not_found`, `duplicate_id`, `same_storage`; preserves both entry dictionaries |

No operation interprets values such as `locked`, `weight`, `capacity`, or `quantity`.
The game decides when to call them. Transfer to the same storage is always a
`same_storage` no-op, including for an absent ID. A rejected transfer leaves both
storages unchanged. The destination copy completes before source removal;
allocation failures preserve the source. Extraction constructs the returned item
before committing removal. These guarantees do not imply thread synchronization.
Because transfer preserves entry properties, the game should change or remove any
destination-specific property (for example, a slot number) when appropriate.

## Saving and loading JSON

`Storage::to_json()` returns a UTF-8 JSON string. `Storage::load_json()` replaces
the receiving storage's items and parameters from that string. The library leaves
file I/O to the game:

```cpp
const std::string save_data = chest.to_json();
// Write save_data to a file, database, or browser storage.

Storage restored;
restored.load_json(save_data);
```

The format is versioned. A minimal snapshot looks like this:

```json
{"version":2,"parameters":{"label":"Chest"},"items":[{"id":"42","item_data":{"type":"sword","durability":80},"properties":{"quantity":2}}]}
```

`to_json()` writes version 2. `load_json()` also accepts version 1 snapshots. It
migrates each old item's `parameters` into `item_data` and initializes its entry
`properties` to `{}`. It does not infer that a legacy `quantity` key belonged to
entry properties; games may move that key explicitly after loading if needed.

IDs are decimal **strings**, so the full 64-bit range survives JavaScript JSON
handling. The loader preserves item order, IDs, nested values, and the difference
between integers and doubles. A new item created after loading receives an ID
above every restored ID. Existing runtime action providers remain attached to the
storage and are not included in JSON.

Loading is a replacement, not a merge. Invalid syntax, unsupported versions,
duplicate or invalid IDs, malformed fields, and nonfinite numbers are rejected
with `std::invalid_argument`; the existing storage remains unchanged. Serialization
also rejects nonfinite doubles and invalid UTF-8. The format accepts up to 128
levels of nesting. A snapshot restores one storage at a time; the game should save
every storage it needs. IDs are unique among newly created items in one linked
library instance, but copying an item or loading the same snapshot into two
storages can intentionally create the same ID in both. Cross-storage ownership
remains the game's responsibility.

## Actions

Register an `ActionProvider` on a storage. It receives the current item snapshot,
read-only storage, and game-supplied `Context` dictionary and returns actions.
The provider may also capture external game state. See `examples/basic.cpp`.

```cpp
// Put the previously extracted item back before trying its actions.
if (removed) bag.add(*removed);
bag.set_action_provider([](const Item&, const Storage&, const Context& context) {
    const auto it = context.find("can_drop");
    const bool enabled = it != context.end() && it->second == Value(true);
    return std::vector<Action>{
        {{"drop", enabled, enabled ? "" : "Dropping is blocked"},
         [](Storage& owner, ItemId id, const Context&) {
             auto dropped = owner.extract(id);
             // The game can place the extracted item in its world here.
         }}
    };
});

Context context{{"can_drop", true}};
auto available = bag.actions(sword.id(), context); // Status and ActionInfo entries
auto execution = bag.execute_action(sword.id(), "drop", context);
```

`actions()` exposes descriptions, not executable callbacks. The provider runs
again on `execute_action()` with the context passed to that call. The game must
pass fresh context; the core cannot refresh external data itself. Omit an action
to hide it, or set `enabled = false` with a reason to show it as unavailable.
The game maps stable action IDs to labels, icons, and localization.

Action IDs must be nonempty and unique within each generated catalog; malformed
catalogs yield `invalid_catalog`. Missing items/actions, disabled actions, and
missing handlers produce explicit statuses without calling a handler. An unset
provider yields an empty ready list. Reset it with `set_action_provider({})`.

Providers should be side-effect-free. Do not retain references to their temporary
item argument in returned handlers: capture values or use the handler's item ID
to read current storage. Captured external objects must outlive their use. The
provider callable is copied per invocation, so keep persistent mutable game state
outside the callable (for example, capture a reference with a suitable lifetime).

Handlers run synchronously and may change storage, extract items, or replace the
provider. `executed` means the handler returned normally; it is not a transactional
guarantee for arbitrary game code. Callback exceptions propagate to the caller,
and earlier changes made by that callback are not rolled back. Concurrent access
to storage or external state requires application-owned synchronization.

## Browser / WebAssembly

Use the same C++ API inside a game compiled with Emscripten. After activating an
Emscripten SDK environment, configure a separate build:

```sh
emcmake cmake -S . -B build/wasm -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm
ctest --test-dir build/wasm --output-on-failure
```

Emscripten's CMake toolchain supplies Node as the cross-compiling emulator for
tests. The example produces `storage_example.html` plus JavaScript and WebAssembly
files. Serve the whole output directory over HTTP and open the HTML page:

```sh
python -m http.server 8080 --directory build/wasm
```

The example prints its result to the Emscripten page/console. It is a core usage
demonstration, not an inventory UI. C++ exceptions are enabled transitively for
Emscripten to match desktop behavior. The library is static and does not require
Emscripten shared-library support. A direct JavaScript/TypeScript API (for a game
not written in C++) would need a separate binding layer; none is included yet.

For the graphical browser example, follow [the WebAssembly UI build](docs/ui.md#build-the-graphical-example).

## Validation

The current implementation has core and UI behavior checks, run through
CTest on Windows/MSVC and WebAssembly/Node. The graphical example builds for both
desktop and WebAssembly. Installed CMake packages were previously checked with
UI-only and raylib consumers, as well as a core-only build.
