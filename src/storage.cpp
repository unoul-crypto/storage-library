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
Item Item::new_instance() const {
    Item result(item_data_, entry_properties_);
    result.adapter_type_ = adapter_type_;
    return result;
}
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
void Storage::ensure_revision_available() const {
    if (revision_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("Storage revision space exhausted");
}
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
    ensure_revision_available();
    items_.push_back(item);
    ++revision_;
    return AddResult::added;
}
std::optional<Item> Storage::extract(ItemId id) {
    const auto it = find(id);
    if (it == items_.end()) return std::nullopt;
    ensure_revision_available();
    // Erase only after the return object has been constructed successfully.
    // In particular, std::map's move constructor can allocate on MSVC.
    struct EraseOnSuccess {
        std::vector<Item>& items;
        std::vector<Item>::iterator position;
        std::uint64_t& revision;
        int exceptions = std::uncaught_exceptions();
        ~EraseOnSuccess() noexcept {
            if (std::uncaught_exceptions() == exceptions) { items.erase(position); ++revision; }
        }
    } commit{items_, it, revision_};
    return std::optional<Item>{*it};
}
TransferResult Storage::transfer_to(ItemId id, Storage& destination) {
    if (this == &destination) return TransferResult::same_storage;
    const auto it = find(id);
    if (it == items_.end()) return TransferResult::item_not_found;
    ensure_revision_available();
    // Destination insertion completes before removing the original.
    if (destination.add(*it) == AddResult::duplicate_id) return TransferResult::duplicate_id;
    items_.erase(it);
    ++revision_;
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
    const auto existing = parameters_.find(key);
    if (existing != parameters_.end() && existing->second == value) return;
    ensure_revision_available();
    parameters_.insert_or_assign(std::move(key), std::move(value));
    ++revision_;
}
bool Storage::remove_parameter(const std::string& key) {
    if (parameters_.find(key) == parameters_.end()) return false;
    ensure_revision_available();
    parameters_.erase(key);
    ++revision_;
    return true;
}
bool Storage::set_item_parameter(ItemId id, std::string key, Value value) {
    return set_item_data_value(id, std::move(key), std::move(value));
}
bool Storage::remove_item_parameter(ItemId id, const std::string& key) {
    return remove_item_data_value(id, key);
}
bool Storage::set_item_data_value(ItemId id, std::string key, Value value) {
    const auto it = find(id);
    if (it == items_.end()) return false;
    const auto existing = it->item_data_.find(key);
    if (existing != it->item_data_.end() && existing->second == value) return true;
    ensure_revision_available();
    it->set_item_data_value(std::move(key), std::move(value));
    ++revision_;
    return true;
}
bool Storage::remove_item_data_value(ItemId id, const std::string& key) {
    const auto it = find(id);
    if (it == items_.end() || it->item_data_.find(key) == it->item_data_.end()) return false;
    ensure_revision_available();
    it->remove_item_data_value(key);
    ++revision_;
    return true;
}
bool Storage::set_entry_property(ItemId id, std::string key, Value value) {
    const auto it = find(id);
    if (it == items_.end()) return false;
    const auto existing = it->entry_properties_.find(key);
    if (existing != it->entry_properties_.end() && existing->second == value) return true;
    ensure_revision_available();
    it->set_entry_property(std::move(key), std::move(value));
    ++revision_;
    return true;
}
bool Storage::remove_entry_property(ItemId id, const std::string& key) {
    const auto it = find(id);
    if (it == items_.end() || it->entry_properties_.find(key) == it->entry_properties_.end()) return false;
    ensure_revision_available();
    it->remove_entry_property(key);
    ++revision_;
    return true;
}
bool Storage::set_item_adapter_type(ItemId id, std::string saved_type) {
    const auto it = find(id);
    if (it == items_.end()) return false;
    if (it->adapter_type_ == saved_type) return true;
    ensure_revision_available();
    it->adapter_type_.swap(saved_type);
    ++revision_;
    return true;
}
bool Storage::replace_item_content(ItemId id, Parameters item_data, Parameters entry_properties) {
    const auto it = find(id);
    if (it == items_.end()) return false;
    if (it->item_data_ == item_data && it->entry_properties_ == entry_properties) return true;
    ensure_revision_available();
    it->item_data_.swap(item_data);
    it->entry_properties_.swap(entry_properties);
    ++revision_;
    return true;
}
std::string Storage::to_json() const {
    Value::Array entries;
    entries.reserve(items_.size());
    for (const auto& item : items_) {
        entries.emplace_back(Value::Object{{"id", std::to_string(item.id())},
                                           {"type", item.adapter_type()},
                                           {"item_data", item.item_data()},
                                           {"properties", item.entry_properties()}});
    }
    return detail::encode_json_value(Value::Object{{"version", 4},
                                                    {"revision", std::to_string(revision_)},
                                                    {"parameters", parameters_},
                                                    {"items", std::move(entries)}});
}
void Storage::load_json(std::string_view json) {
    const Value root = detail::parse_json_value(json);
    const auto* object = std::get_if<Value::Object>(&root.data);
    if (!object || object->count("version") != 1 ||
        object->count("parameters") != 1 || object->count("items") != 1 ||
        (object->at("version") != Value(1) && object->at("version") != Value(2) &&
         object->at("version") != Value(3) && object->at("version") != Value(4)))
        throw std::invalid_argument("Unsupported storage JSON schema or version");
    const auto version = object->at("version").as<std::int64_t>();
    if (object->size() != (version == 4 ? 4u : 3u) ||
        (version == 4 && object->count("revision") != 1))
        throw std::invalid_argument("Invalid storage JSON fields");
    std::uint64_t loaded_revision = 0;
    if (version == 4) {
        const auto* revision_text = std::get_if<std::string>(&object->at("revision").data);
        if (!revision_text || revision_text->empty() ||
            (revision_text->size() > 1 && revision_text->front() == '0'))
            throw std::invalid_argument("Invalid storage revision");
        const auto parsed = std::from_chars(revision_text->data(),
                                            revision_text->data() + revision_text->size(), loaded_revision);
        if (parsed.ec != std::errc{} || parsed.ptr != revision_text->data() + revision_text->size())
            throw std::invalid_argument("Invalid storage revision");
    }
    const bool legacy = version == 1;
    const auto* loaded_parameters = std::get_if<Value::Object>(&object->at("parameters").data);
    const auto* entries = std::get_if<Value::Array>(&object->at("items").data);
    if (!loaded_parameters || !entries) throw std::invalid_argument("Invalid storage JSON fields");

    std::vector<Item> loaded_items;
    loaded_items.reserve(entries->size());
    std::set<ItemId> ids;
    ItemId highest = 0;
    for (const auto& entry : *entries) {
        const auto* fields = std::get_if<Value::Object>(&entry.data);
        const std::size_t expected_fields = version == 1 ? 2u : (version == 2 ? 3u : 4u);
        if (!fields || fields->size() != expected_fields || fields->count("id") != 1 ||
            fields->count(legacy ? "parameters" : "item_data") != 1 ||
            (!legacy && fields->count("properties") != 1) ||
            (version >= 3 && fields->count("type") != 1))
            throw std::invalid_argument("Invalid item JSON fields");
        const auto* id_text = std::get_if<std::string>(&fields->at("id").data);
        const auto* item_data = std::get_if<Value::Object>(&fields->at(legacy ? "parameters" : "item_data").data);
        const auto* properties = legacy ? nullptr : std::get_if<Value::Object>(&fields->at("properties").data);
        const auto* adapter_type = version >= 3 ? std::get_if<std::string>(&fields->at("type").data) : nullptr;
        if (!id_text || !item_data || (!legacy && !properties) || (version >= 3 && !adapter_type) || id_text->empty() ||
            (id_text->size() > 1 && id_text->front() == '0'))
            throw std::invalid_argument("Invalid item ID or data");
        ItemId id = 0;
        const auto parsed = std::from_chars(id_text->data(), id_text->data() + id_text->size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != id_text->data() + id_text->size() ||
            id == 0 || !ids.insert(id).second)
            throw std::invalid_argument("Invalid or duplicate item ID");
        loaded_items.push_back(Item(id, *item_data, legacy ? Parameters{} : *properties,
                                    version >= 3 ? *adapter_type : std::string{}));
        highest = std::max(highest, id);
    }
    Parameters loaded_storage_parameters = *loaded_parameters;
    // No operation below can allocate or invoke application callbacks.
    reserve_ids_through(highest);
    items_.swap(loaded_items);
    parameters_.swap(loaded_storage_parameters);
    revision_ = loaded_revision;
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
