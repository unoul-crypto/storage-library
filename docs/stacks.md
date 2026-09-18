# Optional stack operations

The `game_storage::stacks` module adds explicit quantity operations over the core
`Storage`. The core keeps its original behavior: `add`, `extract`, and `transfer_to`
never inspect quantities or merge entries automatically. Games that do not need
stacks can disable `GAME_STORAGE_BUILD_STACKS`.

```cmake
find_package(game_storage 0.8 CONFIG REQUIRED COMPONENTS stacks)
target_link_libraries(my_game PRIVATE game_storage::stacks)
```

For `add_subdirectory`, link the same target. Include the API with:

```cpp
#include <game_storage/stacks.hpp>
```

## Configure compatibility

Create one `StackOperations` object with a side-effect-free compatibility callback.
The library calls it only for `merge`; the game decides which entries represent
the same stackable item.

```cpp
using namespace game_storage;

StackOperations stacks({
    [](const Item& a, const Item& b) {
        return a.adapter_type() == b.adapter_type() &&
               a.item_data() == b.item_data();
    },
    "quantity" // Optional; this is the default entry-property key.
});
```

The quantity property belongs to `entry_properties`. A missing property means one.
A present quantity must be a positive signed 64-bit integer. Zero, negative values,
doubles, strings, and other value types produce `StackStatus::invalid_quantity`.
Operations never repair invalid data implicitly.

The callback may compare any item data or entry properties needed by the game.
Avoid changing storage or external state from it. Other properties do not affect
compatibility unless the callback checks them.

## Operations

```cpp
Item arrows(Parameters{{"type", "arrow"}}, Parameters{{"quantity", 10}});
chest.add(arrows);

auto taken = stacks.extract_quantity(chest, arrows.id(), 3);
auto split = stacks.split(chest, arrows.id(), 2);
auto moved = stacks.transfer_quantity_to(chest, arrows.id(), 1, backpack);
auto merged = stacks.merge(backpack, moved.item_id, backpack, existing_stack_id);
```

`quantity(item)` and `quantity(storage, id)` return `QuantityResult`. All mutating
operations return `StackResult`: `status` reports the outcome, `item_id` identifies
the created, transferred, or surviving entry, and `extracted` is populated only by
successful `extract_quantity` calls.

- `extract_quantity` returns a detached item. Extracting the full quantity keeps
  the original ID; extracting part creates a new ID and reduces the stored entry.
- `split` requires a strict subset, creates a second entry in the same storage,
  and returns its new ID. Splitting the whole stack returns `not_partial`.
- `transfer_quantity_to` transfers between different storages. A full transfer
  uses the original ID. A partial transfer creates a new ID in the destination.
- `merge` can combine entries in one storage or across two storages. It moves the
  full source quantity into the destination and removes the source. The destination
  keeps its item data and non-quantity entry properties. The source's other entry
  properties are discarded.

Partial entries copy the source item data, adapter type, and all entry properties,
then replace the configured quantity. `Item::new_instance()` exposes the same
new-ID cloning primitive for other optional rule modules.

No operation checks weight, capacity, ownership, maximum stack size, or other game
rules. Check them before calling the technical operation. There is no automatic
merge on add or transfer.

## Statuses and failure behavior

`StackStatus` distinguishes missing source/destination entries, invalid amounts,
invalid or insufficient quantities, incompatible stacks, same-entry/same-storage
requests, overflow, and duplicate IDs. Callback and allocation exceptions propagate.

Before changing storage, partial operations build the new entry and replacement
dictionaries. Destination insertion finishes before the source quantity changes.
Merge prepares the destination first, then extracts the source and commits the
destination using non-throwing dictionary swaps. Under the library's single-threaded,
non-reentrant usage model, reported failures leave existing entries unchanged.

See [the complete example](../examples/stacks.cpp).
