# Game Storage

A C++17 logical core for desktop and WebAssembly games. No graphics, engine APIs,
third-party libraries, capacity rules, slots, stacks, or partial extraction.

## Build and test

Requires CMake 3.21+ and a C++17 compiler. Run from the repository root:

```sh
cmake -S . -B build/desktop
cmake --build build/desktop --config Release
ctest --test-dir build/desktop -C Release --output-on-failure
```

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
find_package(game_storage 0.1 CONFIG REQUIRED)
target_link_libraries(my_game PRIVATE game_storage::game_storage)
```

Pass the installation prefix in `CMAKE_PREFIX_PATH` when configuring the consumer.

```cpp
#include <game_storage/storage.hpp>
using namespace game_storage;

Storage chest({{"label", "Chest"}});
Storage bag;
Item sword({{"type", "sword"}, {"durability", 100}});

chest.add(sword);
chest.set_item_parameter(sword.id(), "durability", 80);
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
- Each new `Item(parameters)` receives a nonzero `uint64_t` ID. Identity is stored
  separately from the parameter dictionary and has no setter. A parameter named
  `"id"` is ordinary application metadata and does not change `item.id()`.
- Copying an item preserves its ID and deep-copies its parameters. To create
  another instance of the same item type, construct `Item(existing.parameters())`.
- IDs are unique across new items within one linked library instance during a
  process/module lifetime. They are not save-file, distributed, or cross-module
  identifiers. Persistence and ID restoration are outside this first version.
- `add` copies an item. Duplicate IDs are rejected within the receiving storage;
  different storages do not share a global ownership registry. Use `transfer_to`
  for movement instead of adding the same snapshot to multiple storages.
- Items remain in insertion order. Removal preserves the order of remaining items;
  transfer appends to the destination. Lookup is linear, suitable for an initial
  small-to-medium game inventory core. There is no automatic stacking.
- All parameter/item/list reads return independent snapshots, including nested
  arrays and dictionaries. Mutating a snapshot never changes storage. Use
  `set_item_parameter` / `remove_item_parameter` for stored items and
  `set_parameter` / `remove_parameter` for storage itself. An unattached `Item`
  also exposes parameter setters.
- Missing lookups return `std::nullopt`, distinct from a present `Value(nullptr)`.
  Parameter setters insert or replace. Removal returns false if the item or key
  is absent; setting a parameter on a missing item returns false.
- Storage objects cannot be copied or moved; use owning pointers when dynamic
  ownership is needed. Obtain item snapshots explicitly through `items()`.

## Technical operations

| Operation | Result |
| --- | --- |
| `add(item)` | `added` or `duplicate_id` |
| `extract(id)` | Whole item or `std::nullopt` |
| `items()` / `item(id)` | List snapshot / optional item snapshot |
| `transfer_to(id, destination)` | `transferred`, `item_not_found`, `duplicate_id`, `same_storage` |

No operation interprets parameters such as `locked`, `weight`, or `capacity`.
The game decides when to call them. Transfer to the same storage is always a
`same_storage` no-op, including for an absent ID. A rejected transfer leaves both
storages unchanged. The destination copy completes before source removal;
allocation failures preserve the source. Extraction constructs the returned item
before committing removal. These guarantees do not imply thread synchronization.

## Actions

Register an `ActionProvider` on a storage. It receives the current item snapshot,
read-only storage, and game-supplied `Context` dictionary and returns actions.
The provider may also capture external game state. See `examples/basic.cpp`.

```cpp
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
