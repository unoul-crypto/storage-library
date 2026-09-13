#include "game_storage/storage.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <exception>
#include <limits>
#include <set>
#include <stdexcept>

namespace game_storage {
namespace detail {
Value parse_json_value(std::string_view text);
std::string encode_json_value(const Value& value);
}
namespace {
std::atomic<ItemId> next_item_id{1};
ItemId next_id() {
    auto value = next_item_id.load(std::memory_order_relaxed);
    for (;;) {
        if (value == std::numeric_limits<ItemId>::max()) {
            throw std::overflow_error("Item ID space exhausted");
        }
        if (next_item_id.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) {
            return value;
        }
    }
}

void reserve_ids_through(ItemId last) noexcept {
    const ItemId desired = last == std::numeric_limits<ItemId>::max() ? last : last + 1;
    auto current = next_item_id.load(std::memory_order_relaxed);
    while (current < desired && !next_item_id.compare_exchange_weak(
        current, desired, std::memory_order_relaxed)) {}
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

Item::Item(Parameters item_data, Parameters entry_properties)
    : id_(next_id()), item_data_(std::move(item_data)), entry_properties_(std::move(entry_properties)) {}
std::optional<Value> Item::item_data_value(const std::string& key) const {
    return lookup(item_data_, key);
}
void Item::set_item_data_value(std::string key, Value value) {
    item_data_.insert_or_assign(std::move(key), std::move(value));
}
bool Item::remove_item_data_value(const std::string& key) { return item_data_.erase(key) != 0; }
std::optional<Value> Item::entry_property(const std::string& key) const {
    return lookup(entry_properties_, key);
}
void Item::set_entry_property(std::string key, Value value) {
    entry_properties_.insert_or_assign(std::move(key), std::move(value));
}
bool Item::remove_entry_property(const std::string& key) { return entry_properties_.erase(key) != 0; }
std::optional<Value> Item::parameter(const std::string& key) const { return item_data_value(key); }
void Item::set_parameter(std::string key, Value value) { set_item_data_value(std::move(key), std::move(value)); }
bool Item::remove_parameter(const std::string& key) { return remove_item_data_value(key); }

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
    return set_item_data_value(id, std::move(key), std::move(value));
}
bool Storage::remove_item_parameter(ItemId id, const std::string& key) {
    return remove_item_data_value(id, key);
}
bool Storage::set_item_data_value(ItemId id, std::string key, Value value) {
    const auto it = find(id);
    if (it == items_.end()) return false;
    it->set_item_data_value(std::move(key), std::move(value));
    return true;
}
bool Storage::remove_item_data_value(ItemId id, const std::string& key) {
    const auto it = find(id);
    return it != items_.end() && it->remove_item_data_value(key);
}
bool Storage::set_entry_property(ItemId id, std::string key, Value value) {
    const auto it = find(id);
    if (it == items_.end()) return false;
    it->set_entry_property(std::move(key), std::move(value));
    return true;
}
bool Storage::remove_entry_property(ItemId id, const std::string& key) {
    const auto it = find(id);
    return it != items_.end() && it->remove_entry_property(key);
}
std::string Storage::to_json() const {
    Value::Array entries;
    entries.reserve(items_.size());
    for (const auto& item : items_) {
        entries.emplace_back(Value::Object{{"id", std::to_string(item.id())},
                                           {"item_data", item.item_data()},
                                           {"properties", item.entry_properties()}});
    }
    return detail::encode_json_value(Value::Object{{"version", 2},
                                                    {"parameters", parameters_},
                                                    {"items", std::move(entries)}});
}
void Storage::load_json(std::string_view json) {
    const Value root = detail::parse_json_value(json);
    const auto* object = std::get_if<Value::Object>(&root.data);
    if (!object || object->size() != 3 || object->count("version") != 1 ||
        object->count("parameters") != 1 || object->count("items") != 1 ||
        (object->at("version") != Value(1) && object->at("version") != Value(2)))
        throw std::invalid_argument("Unsupported storage JSON schema or version");
    const bool legacy = object->at("version") == Value(1);
    const auto* loaded_parameters = std::get_if<Value::Object>(&object->at("parameters").data);
    const auto* entries = std::get_if<Value::Array>(&object->at("items").data);
    if (!loaded_parameters || !entries) throw std::invalid_argument("Invalid storage JSON fields");

    std::vector<Item> loaded_items;
    loaded_items.reserve(entries->size());
    std::set<ItemId> ids;
    ItemId highest = 0;
    for (const auto& entry : *entries) {
        const auto* fields = std::get_if<Value::Object>(&entry.data);
        if (!fields || fields->size() != (legacy ? 2u : 3u) || fields->count("id") != 1 ||
            fields->count(legacy ? "parameters" : "item_data") != 1 ||
            (!legacy && fields->count("properties") != 1))
            throw std::invalid_argument("Invalid item JSON fields");
        const auto* id_text = std::get_if<std::string>(&fields->at("id").data);
        const auto* item_data = std::get_if<Value::Object>(&fields->at(legacy ? "parameters" : "item_data").data);
        const auto* properties = legacy ? nullptr : std::get_if<Value::Object>(&fields->at("properties").data);
        if (!id_text || !item_data || (!legacy && !properties) || id_text->empty() ||
            (id_text->size() > 1 && id_text->front() == '0'))
            throw std::invalid_argument("Invalid item ID or data");
        ItemId id = 0;
        const auto parsed = std::from_chars(id_text->data(), id_text->data() + id_text->size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != id_text->data() + id_text->size() ||
            id == 0 || !ids.insert(id).second)
            throw std::invalid_argument("Invalid or duplicate item ID");
        loaded_items.push_back(Item(id, *item_data, legacy ? Parameters{} : *properties));
        highest = std::max(highest, id);
    }
    Parameters loaded_storage_parameters = *loaded_parameters;
    // No operation below can allocate or invoke application callbacks.
    reserve_ids_through(highest);
    items_.swap(loaded_items);
    parameters_.swap(loaded_storage_parameters);
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
