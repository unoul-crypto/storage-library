#include "game_storage/sync.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <stdexcept>

namespace game_storage::detail {
Value parse_json_value(std::string_view text);
std::string encode_json_value(const Value& value);
}

namespace game_storage::sync {
namespace {
std::uint64_t checksum(std::string_view text) noexcept {
    std::uint64_t result = 14695981039346656037ULL;
    for (unsigned char byte : text) {
        result ^= byte;
        result *= 1099511628211ULL;
    }
    return result;
}

std::uint64_t decimal(const Value& value, bool allow_zero = true) {
    const auto* text = std::get_if<std::string>(&value.data);
    if (!text || text->empty() || (text->size() > 1 && text->front() == '0'))
        throw std::invalid_argument("Invalid sync decimal string");
    std::uint64_t number = 0;
    const auto parsed = std::from_chars(text->data(), text->data() + text->size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size() ||
        (!allow_zero && number == 0))
        throw std::invalid_argument("Invalid sync decimal string");
    return number;
}

const Value& field(const Value::Object& object, const char* name) {
    const auto it = object.find(name);
    if (it == object.end()) throw std::invalid_argument("Missing sync field");
    return it->second;
}

const Value::Object& object(const Value& value) {
    const auto* result = std::get_if<Value::Object>(&value.data);
    if (!result) throw std::invalid_argument("Expected sync object");
    return *result;
}

const Value::Array& array(const Value& value) {
    const auto* result = std::get_if<Value::Array>(&value.data);
    if (!result) throw std::invalid_argument("Expected sync array");
    return *result;
}

ItemState state(const Item& item) {
    return {item.id(), item.adapter_type(), item.item_data(), item.entry_properties()};
}

bool same(const ItemState& a, const ItemState& b) {
    return a.id == b.id && a.type == b.type && a.item_data == b.item_data &&
           a.properties == b.properties;
}

Value item_value(const ItemState& item) {
    return Value::Object{{"id", std::to_string(item.id)}, {"type", item.type},
                         {"item_data", item.item_data}, {"properties", item.properties}};
}

ItemState read_item(const Value& value) {
    const auto& fields = object(value);
    if (fields.size() != 4) throw std::invalid_argument("Invalid sync item fields");
    const auto* type = std::get_if<std::string>(&field(fields, "type").data);
    if (!type) throw std::invalid_argument("Invalid sync item type");
    return {decimal(field(fields, "id"), false), *type,
            object(field(fields, "item_data")), object(field(fields, "properties"))};
}

void validate(const Delta& delta) {
    if (delta.target_revision < delta.base_revision ||
        (delta.target_revision == delta.base_revision &&
         (delta.base_checksum != delta.target_checksum || !delta.removed.empty() ||
          !delta.upserted.empty() || delta.parameters || delta.order)))
        throw std::invalid_argument("Invalid sync revision range");
    std::set<ItemId> touched;
    for (ItemId id : delta.removed)
        if (id == 0 || !touched.insert(id).second)
            throw std::invalid_argument("Duplicate or zero sync item ID");
    for (const auto& item : delta.upserted)
        if (item.id == 0 || !touched.insert(item.id).second)
            throw std::invalid_argument("Duplicate or zero sync item ID");
    if (delta.order) {
        std::set<ItemId> ordered;
        for (ItemId id : *delta.order)
            if (id == 0 || !ordered.insert(id).second)
                throw std::invalid_argument("Duplicate or zero sync order ID");
    }
}
} // namespace

std::string Delta::to_json() const {
    validate(*this);
    Value::Array removed_values, upserted_values;
    for (ItemId id : removed) removed_values.emplace_back(std::to_string(id));
    for (const auto& item : upserted) upserted_values.push_back(item_value(item));
    Value order_value = nullptr;
    if (order) {
        Value::Array ids;
        for (ItemId id : *order) ids.emplace_back(std::to_string(id));
        order_value = std::move(ids);
    }
    return detail::encode_json_value(Value::Object{
        {"version", 1}, {"base_revision", std::to_string(base_revision)},
        {"target_revision", std::to_string(target_revision)},
        {"base_checksum", std::to_string(base_checksum)},
        {"target_checksum", std::to_string(target_checksum)},
        {"removed", std::move(removed_values)}, {"upserted", std::move(upserted_values)},
        {"parameters", parameters ? Value(*parameters) : Value(nullptr)},
        {"order", std::move(order_value)}});
}

Delta Delta::from_json(std::string_view json) {
    const auto parsed = detail::parse_json_value(json);
    const auto& root = object(parsed);
    if (root.size() != 9 || field(root, "version") != Value(1))
        throw std::invalid_argument("Unsupported sync delta version or fields");
    Delta result;
    result.base_revision = decimal(field(root, "base_revision"));
    result.target_revision = decimal(field(root, "target_revision"));
    result.base_checksum = decimal(field(root, "base_checksum"));
    result.target_checksum = decimal(field(root, "target_checksum"));
    for (const auto& entry : array(field(root, "removed")))
        result.removed.push_back(decimal(entry, false));
    for (const auto& entry : array(field(root, "upserted")))
        result.upserted.push_back(read_item(entry));
    const auto& parameter_value = field(root, "parameters");
    if (!std::holds_alternative<std::nullptr_t>(parameter_value.data))
        result.parameters = object(parameter_value);
    const auto& order_value = field(root, "order");
    if (!std::holds_alternative<std::nullptr_t>(order_value.data)) {
        std::vector<ItemId> ids;
        for (const auto& entry : array(order_value)) ids.push_back(decimal(entry, false));
        result.order = std::move(ids);
    }
    validate(result);
    return result;
}

Delta make_delta(const Storage& before, const Storage& after) {
    const auto base_json = before.to_json(), target_json = after.to_json();
    if (after.revision() < before.revision() ||
        (after.revision() == before.revision() && base_json != target_json))
        throw std::invalid_argument("Snapshots do not have an ordered revision history");
    Delta result;
    result.base_revision = before.revision(); result.target_revision = after.revision();
    result.base_checksum = checksum(base_json); result.target_checksum = checksum(target_json);
    if (before.parameters() != after.parameters()) result.parameters = after.parameters();

    const auto previous = before.items(), current = after.items();
    std::map<ItemId, ItemState> old_items;
    std::set<ItemId> new_ids;
    std::vector<ItemId> expected_order, actual_order;
    for (const auto& item : previous) old_items.emplace(item.id(), state(item));
    for (const auto& item : current) {
        new_ids.insert(item.id());
        actual_order.push_back(item.id());
    }
    for (const auto& item : previous) {
        if (!new_ids.count(item.id())) result.removed.push_back(item.id());
        else expected_order.push_back(item.id());
    }
    for (const auto& item : current) {
        const auto fresh = state(item);
        const auto old = old_items.find(item.id());
        if (old == old_items.end()) expected_order.push_back(item.id());
        if (old == old_items.end() || !same(old->second, fresh)) result.upserted.push_back(fresh);
    }
    if (expected_order != actual_order) result.order = std::move(actual_order);
    return result;
}

ApplyStatus apply_delta(Storage& storage, const Delta& delta) {
    if (storage.revision() != delta.base_revision) return ApplyStatus::revision_mismatch;
    const auto original = storage.to_json();
    if (checksum(original) != delta.base_checksum) return ApplyStatus::state_mismatch;
    try { validate(delta); }
    catch (const std::invalid_argument&) { return ApplyStatus::invalid_delta; }

    try {
        auto root = detail::parse_json_value(original);
        auto& fields = std::get<Value::Object>(root.data);
        auto& entries = std::get<Value::Array>(fields.at("items").data);
        auto find_entry = [&entries](ItemId id) {
            return std::find_if(entries.begin(), entries.end(), [id](const Value& value) {
                return decimal(field(object(value), "id"), false) == id;
            });
        };
        for (ItemId id : delta.removed) {
            const auto it = find_entry(id);
            if (it == entries.end()) return ApplyStatus::invalid_delta;
            entries.erase(it);
        }
        for (const auto& item : delta.upserted) {
            const auto it = find_entry(item.id);
            if (it == entries.end()) entries.push_back(item_value(item));
            else *it = item_value(item);
        }
        if (delta.order) {
            if (delta.order->size() != entries.size()) return ApplyStatus::invalid_delta;
            Value::Array ordered;
            ordered.reserve(entries.size());
            for (ItemId id : *delta.order) {
                const auto it = find_entry(id);
                if (it == entries.end()) return ApplyStatus::invalid_delta;
                ordered.push_back(*it);
            }
            entries.swap(ordered);
        }
        if (delta.parameters) fields["parameters"] = *delta.parameters;
        fields["revision"] = std::to_string(delta.target_revision);
        const auto candidate = detail::encode_json_value(root);
        if (checksum(candidate) != delta.target_checksum) return ApplyStatus::invalid_delta;
        storage.load_json(candidate);
        return ApplyStatus::applied;
    } catch (const std::invalid_argument&) {
        return ApplyStatus::invalid_delta;
    }
}
} // namespace game_storage::sync
