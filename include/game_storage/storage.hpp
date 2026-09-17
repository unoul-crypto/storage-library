#pragma once

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace game_storage {

// Owning values only: nested snapshots never share mutable data with storage.
struct Value {
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;
    using Data = std::variant<std::nullptr_t, bool, std::int64_t, double,
                              std::string, Array, Object>;
    Data data = nullptr;

    Value() = default;
    Value(std::nullptr_t) : data(nullptr) {}
    Value(bool value) : data(value) {}
    template<class T, std::enable_if_t<std::is_integral_v<T> &&
                                     std::is_signed_v<T>, int> = 0>
    Value(T value) : data(static_cast<std::int64_t>(value)) {}
    Value(double value) : data(value) {}
    Value(const char* value) : data(std::string(value)) {}
    Value(std::string value) : data(std::move(value)) {}
    Value(Array value) : data(std::move(value)) {}
    Value(Object value) : data(std::move(value)) {}

    template<class T> const T& as() const { return std::get<T>(data); }
    template<class T> T& as() { return std::get<T>(data); }
    friend bool operator==(const Value& a, const Value& b) { return a.data == b.data; }
    friend bool operator!=(const Value& a, const Value& b) { return !(a == b); }
};

using Parameters = Value::Object;
using Context = Parameters;
using ItemId = std::uint64_t;
class ItemAdapterRegistry;

class Item {
public:
    // Copying preserves identity; constructing a new Item generates a new ID.
    // Both dictionaries are arbitrary data; the core does not interpret quantity.
    explicit Item(Parameters item_data = {}, Parameters entry_properties = {});
    Item(std::initializer_list<Parameters::value_type> item_data)
        : Item(Parameters(item_data)) {}
    ItemId id() const noexcept { return id_; }
    const std::string& adapter_type() const noexcept { return adapter_type_; }
    Parameters item_data() const { return item_data_; }
    std::optional<Value> item_data_value(const std::string& key) const;
    void set_item_data_value(std::string key, Value value);
    bool remove_item_data_value(const std::string& key);
    Parameters entry_properties() const { return entry_properties_; }
    std::optional<Value> entry_property(const std::string& key) const;
    void set_entry_property(std::string key, Value value);
    bool remove_entry_property(const std::string& key);
    // Compatibility names for item_data in the original API.
    Parameters parameters() const { return item_data(); }
    std::optional<Value> parameter(const std::string& key) const;
    void set_parameter(std::string key, Value value);
    bool remove_parameter(const std::string& key);

private:
    friend class Storage;
    friend class ItemAdapterRegistry;
    Item(ItemId id, Parameters item_data, Parameters entry_properties, std::string adapter_type = {})
        : id_(id), adapter_type_(std::move(adapter_type)), item_data_(std::move(item_data)),
          entry_properties_(std::move(entry_properties)) {}
    ItemId id_;
    std::string adapter_type_;
    Parameters item_data_;
    Parameters entry_properties_;
};

enum class AddResult { added, duplicate_id };
enum class TransferResult { transferred, item_not_found, duplicate_id, same_storage };

class Storage;

struct ActionInfo {
    std::string id;
    bool enabled = true;
    std::string disabled_reason;
};

struct Action {
    ActionInfo info;
    std::function<void(Storage&, ItemId, const Context&)> execute;
};

using ActionProvider = std::function<std::vector<Action>(
    const Item&, const Storage&, const Context&)>;

enum class ActionStatus {
    ready, executed, item_not_found, action_not_found,
    disabled, missing_handler, invalid_catalog
};

struct ActionList {
    ActionStatus status = ActionStatus::ready;
    std::vector<ActionInfo> actions;
};

struct ActionResult {
    ActionStatus status;
    std::string reason;
};

// Single-threaded mutable container. The game owns synchronization and rules.
class Storage {
public:
    explicit Storage(Parameters parameters = {});
    Storage(std::initializer_list<Parameters::value_type> parameters)
        : Storage(Parameters(parameters)) {}
    // Storage has identity: avoid accidental copies containing the same items.
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    Storage(Storage&&) = delete;
    Storage& operator=(Storage&&) = delete;

    AddResult add(const Item& item);
    std::optional<Item> extract(ItemId id);
    TransferResult transfer_to(ItemId id, Storage& destination);
    std::vector<Item> items() const { return items_; }
    std::optional<Item> item(ItemId id) const;
    bool contains(ItemId id) const;
    std::size_t size() const noexcept { return items_.size(); }

    Parameters parameters() const { return parameters_; }
    std::optional<Value> parameter(const std::string& key) const;
    void set_parameter(std::string key, Value value);
    bool remove_parameter(const std::string& key);
    bool set_item_parameter(ItemId id, std::string key, Value value);
    bool remove_item_parameter(ItemId id, const std::string& key);
    // Mutate the two independent dictionaries of a stored entry by its ID.
    bool set_item_data_value(ItemId id, std::string key, Value value);
    bool remove_item_data_value(ItemId id, const std::string& key);
    bool set_entry_property(ItemId id, std::string key, Value value);
    bool remove_entry_property(ItemId id, const std::string& key);
    bool set_item_adapter_type(ItemId id, std::string saved_type);
    // Atomically replaces both dictionaries, preserving the entry ID and order.
    bool replace_item_content(ItemId id, Parameters item_data, Parameters entry_properties);

    // Versioned JSON snapshot; ID strings preserve the full uint64_t range.
    std::string to_json() const;
    void load_json(std::string_view json); // Replaces data atomically; throws on invalid input.

    void set_action_provider(ActionProvider provider);
    ActionList actions(ItemId id, const Context& context = {}) const;
    ActionResult execute_action(ItemId id, const std::string& action_id,
                                const Context& context = {});

private:
    std::vector<Item>::iterator find(ItemId id);
    std::vector<Item>::const_iterator find(ItemId id) const;
    std::vector<Item> items_;
    Parameters parameters_;
    ActionProvider action_provider_;
};

} // namespace game_storage
