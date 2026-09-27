#pragma once

#include "storage.hpp"

namespace game_storage::sync {

// A complete entry state used by an incremental update. IDs are preserved.
struct ItemState {
    ItemId id = 0;
    std::string type;
    Parameters item_data;
    Parameters properties;
};

struct Delta {
    std::uint64_t base_revision = 0, target_revision = 0;
    std::uint64_t base_checksum = 0, target_checksum = 0;
    std::vector<ItemId> removed;
    std::vector<ItemState> upserted;
    std::optional<Parameters> parameters;
    std::optional<std::vector<ItemId>> order;

    std::string to_json() const;
    static Delta from_json(std::string_view json);
};

enum class ApplyStatus { applied, revision_mismatch, state_mismatch, invalid_delta };

// before and after must be snapshots of the same storage timeline, in order.
Delta make_delta(const Storage& before, const Storage& after);
// Never changes storage on a non-applied result. Runtime action provider survives.
ApplyStatus apply_delta(Storage& storage, const Delta& delta);

} // namespace game_storage::sync
