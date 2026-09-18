#include "game_storage/stacks.hpp"

#include <limits>
#include <stdexcept>

namespace game_storage {
namespace {
void put_quantity(Parameters& properties, const std::string& key, std::int64_t value) {
    properties.insert_or_assign(key, Value(value));
}

StackStatus map_transfer(TransferResult status) {
    switch (status) {
    case TransferResult::transferred: return StackStatus::success;
    case TransferResult::item_not_found: return StackStatus::item_not_found;
    case TransferResult::duplicate_id: return StackStatus::duplicate_id;
    case TransferResult::same_storage: return StackStatus::same_storage;
    }
    return StackStatus::item_not_found;
}
}

StackOperations::StackOperations(StackConfig config) : config_(std::move(config)) {
    if (!config_.can_stack) throw std::invalid_argument("Stack compatibility callback is required");
    if (config_.quantity_property.empty()) throw std::invalid_argument("Quantity property must not be empty");
}

QuantityResult StackOperations::quantity(const Item& item) const {
    const auto value = item.entry_property(config_.quantity_property);
    if (!value) return {StackStatus::success, 1};
    const auto* integer = std::get_if<std::int64_t>(&value->data);
    if (!integer || *integer <= 0) return {StackStatus::invalid_quantity, 0};
    return {StackStatus::success, *integer};
}

QuantityResult StackOperations::quantity(const Storage& storage, ItemId id) const {
    const auto item = storage.item(id);
    if (!item) return {StackStatus::item_not_found, 0};
    return quantity(*item);
}

StackResult StackOperations::extract_quantity(Storage& storage, ItemId id,
                                               std::int64_t amount) const {
    if (amount <= 0) return {StackStatus::invalid_amount};
    const auto source = storage.item(id);
    if (!source) return {StackStatus::item_not_found};
    const auto count = quantity(*source);
    if (count.status != StackStatus::success) return {count.status};
    if (amount > count.value) return {StackStatus::insufficient_quantity};
    if (amount == count.value) {
        auto extracted = storage.extract(id);
        return {StackStatus::success, id, std::move(extracted)};
    }

    Item part = source->new_instance();
    part.set_entry_property(config_.quantity_property, amount);
    auto remaining = source->entry_properties();
    put_quantity(remaining, config_.quantity_property, count.value - amount);
    StackResult result{StackStatus::success, part.id(), part};
    storage.replace_item_content(id, source->item_data(), std::move(remaining));
    return result;
}

StackResult StackOperations::split(Storage& storage, ItemId id, std::int64_t amount) const {
    if (amount <= 0) return {StackStatus::invalid_amount};
    const auto source = storage.item(id);
    if (!source) return {StackStatus::item_not_found};
    const auto count = quantity(*source);
    if (count.status != StackStatus::success) return {count.status};
    if (amount > count.value) return {StackStatus::insufficient_quantity};
    if (amount == count.value) return {StackStatus::not_partial};

    Item part = source->new_instance();
    part.set_entry_property(config_.quantity_property, amount);
    auto source_data = source->item_data();
    auto remaining = source->entry_properties();
    put_quantity(remaining, config_.quantity_property, count.value - amount);
    if (storage.add(part) != AddResult::added) return {StackStatus::duplicate_id};
    storage.replace_item_content(id, std::move(source_data), std::move(remaining));
    return {StackStatus::success, part.id()};
}

StackResult StackOperations::transfer_quantity_to(Storage& source_storage, ItemId id,
                                                   std::int64_t amount,
                                                   Storage& destination) const {
    if (amount <= 0) return {StackStatus::invalid_amount};
    if (&source_storage == &destination) return {StackStatus::same_storage};
    const auto source = source_storage.item(id);
    if (!source) return {StackStatus::item_not_found};
    const auto count = quantity(*source);
    if (count.status != StackStatus::success) return {count.status};
    if (amount > count.value) return {StackStatus::insufficient_quantity};
    if (amount == count.value) {
        const auto status = map_transfer(source_storage.transfer_to(id, destination));
        return {status, status == StackStatus::success ? id : 0};
    }

    Item part = source->new_instance();
    part.set_entry_property(config_.quantity_property, amount);
    auto source_data = source->item_data();
    auto remaining = source->entry_properties();
    put_quantity(remaining, config_.quantity_property, count.value - amount);
    if (destination.add(part) != AddResult::added) return {StackStatus::duplicate_id};
    source_storage.replace_item_content(id, std::move(source_data), std::move(remaining));
    return {StackStatus::success, part.id()};
}

StackResult StackOperations::merge(Storage& source_storage, ItemId source_id,
                                   Storage& destination, ItemId destination_id) const {
    if (&source_storage == &destination && source_id == destination_id)
        return {StackStatus::same_item};
    const auto source = source_storage.item(source_id);
    if (!source) return {StackStatus::item_not_found};
    const auto target = destination.item(destination_id);
    if (!target) return {StackStatus::destination_not_found};
    const auto source_count = quantity(*source);
    if (source_count.status != StackStatus::success) return {source_count.status};
    const auto target_count = quantity(*target);
    if (target_count.status != StackStatus::success) return {target_count.status};
    if (!config_.can_stack(*source, *target)) return {StackStatus::incompatible};
    if (source_count.value > std::numeric_limits<std::int64_t>::max() - target_count.value)
        return {StackStatus::quantity_overflow};

    auto target_data = target->item_data();
    auto combined = target->entry_properties();
    put_quantity(combined, config_.quantity_property, source_count.value + target_count.value);
    auto removed = source_storage.extract(source_id);
    if (!removed) return {StackStatus::item_not_found};
    destination.replace_item_content(destination_id, std::move(target_data), std::move(combined));
    return {StackStatus::success, destination_id};
}

} // namespace game_storage
