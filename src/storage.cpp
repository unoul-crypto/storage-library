#include "game_storage/storage.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <set>
#include <stdexcept>

namespace game_storage {
namespace {
ItemId next_id() {
    static std::atomic<ItemId> next{1};
    auto value = next.load(std::memory_order_relaxed);
    for (;;) {
        if (value == std::numeric_limits<ItemId>::max()) {
            throw std::overflow_error("Item ID space exhausted");
        }
        if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) {
            return value;
        }
    }
}

std::optional<Value> lookup(const Parameters& parameters, const std::string& key) {
    const auto it = parameters.find(key);
    if (it == parameters.end()) return std::nullopt;
    return it->second;
}

bool valid_catalog(const std::vector<Action>& actions) {
    std::set<std::string> ids;
    for (const auto& action : actions) {
        if (action.info.id.empty() || !ids.insert(action.info.id).second) return false;
    }
    return true;
}
} // namespace

Item::Item(Parameters parameters) : id_(next_id()), parameters_(std::move(parameters)) {}
std::optional<Value> Item::parameter(const std::string& key) const {
    return lookup(parameters_, key);
}
void Item::set_parameter(std::string key, Value value) {
    parameters_.insert_or_assign(std::move(key), std::move(value));
}
bool Item::remove_parameter(const std::string& key) { return parameters_.erase(key) != 0; }

Storage::Storage(Parameters parameters) : parameters_(std::move(parameters)) {}
std::vector<Item>::iterator Storage::find(ItemId id) {
    return std::find_if(items_.begin(), items_.end(),
                        [id](const Item& item) { return item.id() == id; });
}
std::vector<Item>::const_iterator Storage::find(ItemId id) const {
    return std::find_if(items_.begin(), items_.end(),
                        [id](const Item& item) { return item.id() == id; });
}
bool Storage::contains(ItemId id) const { return find(id) != items_.end(); }
AddResult Storage::add(const Item& item) {
    if (contains(item.id())) return AddResult::duplicate_id;
    items_.push_back(item);
    return AddResult::added;
}
std::optional<Item> Storage::extract(ItemId id) {
    const auto it = find(id);
    if (it == items_.end()) return std::nullopt;
    // Erase only after the return object has been constructed successfully.
    // In particular, std::map's move constructor can allocate on MSVC.
    struct EraseOnSuccess {
        std::vector<Item>& items;
        std::vector<Item>::iterator position;
        int exceptions = std::uncaught_exceptions();
        ~EraseOnSuccess() noexcept {
            if (std::uncaught_exceptions() == exceptions) items.erase(position);
        }
    } commit{items_, it};
    return std::optional<Item>{*it};
}
TransferResult Storage::transfer_to(ItemId id, Storage& destination) {
    if (this == &destination) return TransferResult::same_storage;
    const auto it = find(id);
    if (it == items_.end()) return TransferResult::item_not_found;
    // Destination insertion completes before removing the original.
    if (destination.add(*it) == AddResult::duplicate_id) return TransferResult::duplicate_id;
    items_.erase(it);
    return TransferResult::transferred;
}
std::optional<Item> Storage::item(ItemId id) const {
    const auto it = find(id);
    if (it == items_.end()) return std::nullopt;
    return *it;
}
std::optional<Value> Storage::parameter(const std::string& key) const {
    return lookup(parameters_, key);
}
void Storage::set_parameter(std::string key, Value value) {
    parameters_.insert_or_assign(std::move(key), std::move(value));
}
bool Storage::remove_parameter(const std::string& key) { return parameters_.erase(key) != 0; }
bool Storage::set_item_parameter(ItemId id, std::string key, Value value) {
    const auto it = find(id);
    if (it == items_.end()) return false;
    it->set_parameter(std::move(key), std::move(value));
    return true;
}
bool Storage::remove_item_parameter(ItemId id, const std::string& key) {
    const auto it = find(id);
    return it != items_.end() && it->remove_parameter(key);
}
void Storage::set_action_provider(ActionProvider provider) {
    action_provider_ = std::move(provider);
}
ActionList Storage::actions(ItemId id, const Context& context) const {
    const auto snapshot = item(id);
    if (!snapshot) return {ActionStatus::item_not_found, {}};
    if (!action_provider_) return {};
    // Keep callable alive if application code replaces the provider during a call.
    const auto provider = action_provider_;
    const auto catalog = provider(*snapshot, *this, context);
    if (!valid_catalog(catalog)) return {ActionStatus::invalid_catalog, {}};
    ActionList result;
    for (const auto& action : catalog) result.actions.push_back(action.info);
    return result;
}
ActionResult Storage::execute_action(ItemId id, const std::string& action_id,
                                     const Context& context) {
    const auto snapshot = item(id);
    if (!snapshot) return {ActionStatus::item_not_found, {}};
    if (!action_provider_) return {ActionStatus::action_not_found, {}};
    const auto provider = action_provider_;
    const auto catalog = provider(*snapshot, *this, context);
    if (!valid_catalog(catalog)) return {ActionStatus::invalid_catalog, {}};
    if (!contains(id)) return {ActionStatus::item_not_found, {}};
    const auto action = std::find_if(catalog.begin(), catalog.end(),
        [&action_id](const Action& entry) { return entry.info.id == action_id; });
    if (action == catalog.end()) return {ActionStatus::action_not_found, {}};
    if (!action->info.enabled) return {ActionStatus::disabled, action->info.disabled_reason};
    if (!action->execute) return {ActionStatus::missing_handler, {}};
    action->execute(*this, id, context);
    return {ActionStatus::executed, {}};
}

static_assert(std::is_nothrow_move_assignable_v<Item>, "Erase must not throw during transfer");
} // namespace game_storage
