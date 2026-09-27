# Optional synchronization module

`game_storage::sync` compares two snapshots of the same storage timeline and
produces an incremental `Delta`. It depends only on the core. Enable it with
`GAME_STORAGE_BUILD_SYNC=ON` (the standalone default), or disable it when no
synchronization is needed.

```cmake
find_package(game_storage 0.11 CONFIG REQUIRED COMPONENTS sync)
target_link_libraries(my_game PRIVATE game_storage::sync)
```

Include `<game_storage/sync.hpp>`. The game owns networking, storage identity,
permissions, and decisions about which state is authoritative. This module
handles state comparison and checked application; it never executes actions or
game rules on the receiving side.

## Send an update

Keep a baseline snapshot for each client. A `Storage` can load one from JSON
without sharing mutable data with the server's live storage:

```cpp
using namespace game_storage;

Storage server;
Storage baseline;
baseline.load_json(server.to_json());

// Later, after game-validated changes to server:
const std::string target_snapshot = server.to_json();
Storage target;
target.load_json(target_snapshot);
const auto delta = sync::make_delta(baseline, target);
const std::string packet = delta.to_json();
// Send packet using the game's transport, tagged with the game's storage ID.

// Once that client confirms the update, replace its baseline:
baseline.load_json(target_snapshot);
```

Retain the target snapshot until acknowledgement. If the server changes again before
acknowledgement, recompute from the client's last confirmed baseline. Retain
per-client baselines only for the interval needed; when unavailable, send a full
`Storage::to_json()` snapshot.

## Receive an update

```cpp
const auto received = sync::Delta::from_json(packet);
switch (sync::apply_delta(client_storage, received)) {
case sync::ApplyStatus::applied:
    break;
case sync::ApplyStatus::revision_mismatch:
case sync::ApplyStatus::state_mismatch:
case sync::ApplyStatus::invalid_delta:
    // Request an authoritative full snapshot or a fresh delta.
    break;
}
```

`revision_mismatch` means the local revision differs from the packet's base.
`state_mismatch` means revisions match but the full base snapshot's checksum
differs, as can happen with a different storage or timeline. `invalid_delta`
means the packet cannot reconstruct its claimed target. None of these results
changes the receiving storage. An applied delta also preserves the local runtime
action provider. Parsing malformed JSON throws `std::invalid_argument`; handle
that at the network boundary. Allocation failures and invalid local data may
also throw. Use full snapshots to recover when delta application fails.

The packet contains base and target revisions, checksums, removed IDs, complete
states for added or changed entries, an optional replacement for storage
parameters, and an optional item order. Missing optional fields in the C++
`Delta` mean "unchanged"; the wire format represents them as `null`. IDs,
revisions, and checksums are decimal strings in JSON, preserving 64-bit values
in browser clients. Deltas can span multiple primitive operations. An unchanged
snapshot produces an empty delta.

The checksums detect accidental divergence; they are **not** authentication or
protection against malicious clients. The server must still validate requests
and send only authorized state. The game should also cap incoming packet size.
`make_delta` requires ordered snapshots from the
same timeline. Revisions alone do not identify a storage, so the game must use
its own stable storage ID in the surrounding protocol. The module is synchronous
and follows the core's single-threaded usage model.
