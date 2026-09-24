#pragma once

#include "storage.hpp"

namespace game_storage {

enum class StackStatus {
    success,
    item_not_found,
    destination_not_found,
    invalid_amount,
    invalid_quantity,
    insufficient_quantity,
    not_partial,
    same_item,
    same_storage,
    incompatible,
    quantity_overflow,
    duplicate_id,
    stack_limit_exceeded,
    invalid_stack_limit
};

struct QuantityResult {
    StackStatus status = StackStatus::success;
    std::int64_t value = 0;
};

struct StackResult {
    StackStatus status = StackStatus::success;
    ItemId item_id = 0;             // Created, transferred, or surviving destination ID.
    std::optional<Item> extracted;  // Set only by extract_quantity on success.

    StackResult() = default;
    StackResult(StackStatus result, ItemId id = 0, std::optional<Item> item = std::nullopt)
        : status(result), item_id(id), extracted(std::move(item)) {}
};

using StackPredicate = std::function<bool(const Item&, const Item&)>;
using StackLimit = std::function<std::int64_t(const Item&)>;

struct StackConfig {
    StackPredicate can_stack;
    std::string quantity_property = "quantity";
    StackLimit max_quantity; // Optional; absence means no limit.
};

// Optional rules layer. Storage itself does not interpret quantities.
class StackOperations {
public:
    explicit StackOperations(StackConfig config);

    // A missing quantity property means one. Present values must be positive int64.
    QuantityResult quantity(const Item& item) const;
    QuantityResult quantity(const Storage& storage, ItemId id) const;
    QuantityResult max_quantity(const Item& item) const;

    // Full extraction preserves the original ID; partial extraction creates a new ID.
    StackResult extract_quantity(Storage& storage, ItemId id, std::int64_t amount) const;
    // Splits a strict subset into a second entry in the same storage.
    StackResult split(Storage& storage, ItemId id, std::int64_t amount) const;
    // Full transfer preserves the original ID; partial transfer creates a new ID.
    StackResult transfer_quantity_to(Storage& source, ItemId id, std::int64_t amount,
                                     Storage& destination) const;
    // Moves all of source_id into destination_id after can_stack approves them.
    StackResult merge(Storage& source, ItemId source_id,
                      Storage& destination, ItemId destination_id) const;

private:
    StackConfig config_;
};

} // namespace game_storage
