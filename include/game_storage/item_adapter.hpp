#pragma once

#include "storage.hpp"
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <typeinfo>

namespace game_storage {

// Optional boundary between a game type and the two dictionaries of an Item.
// Callbacks should only modify their arguments, never the storage being updated.
template<class T>
class ItemAdapter {
public:
    using Encoder = std::function<void(const T&, Parameters&, Parameters&)>;
    using Decoder = std::function<T(const Parameters&, const Parameters&)>;

    ItemAdapter(Encoder encode, Decoder decode)
        : encode_(std::move(encode)), decode_(std::move(decode)) {
        if (!encode_ || !decode_) throw std::invalid_argument("Item adapter requires both callbacks");
    }

    // Creates a new identity. The encoder may augment the supplied properties.
    Item to_item(const T& value, Parameters properties = {}) const {
        Parameters data;
        encode_(value, data, properties);
        return Item(std::move(data), std::move(properties));
    }

    // Returns an independent game object. Type/schema validation belongs to decode.
    T from_item(const Item& item) const {
        return decode_(item.item_data(), item.entry_properties());
    }

    // Applies an encoder to copies, then commits both dictionaries together.
    // Preserves identity and all keys not modified by the encoder.
    bool update(Storage& storage, ItemId id, const T& value) const {
        const auto snapshot = storage.item(id);
        if (!snapshot) return false;
        auto data = snapshot->item_data();
        auto properties = snapshot->entry_properties();
        encode_(value, data, properties);
        return storage.replace_item_content(id, std::move(data), std::move(properties));
    }

private:
    Encoder encode_;
    Decoder decode_;
};

// Selects adapters through stable type IDs stored in Item and JSON snapshots.
// One C++ type and one saved type ID may each be registered only once.
class ItemAdapterRegistry {
public:
    template<class T>
    void register_adapter(std::string saved_type, ItemAdapter<T> adapter) {
        if (saved_type.empty()) throw std::invalid_argument("Saved adapter type must not be empty");
        const std::type_index cpp_type(typeid(T));
        if (by_saved_type_.count(saved_type) || saved_type_by_cpp_.count(cpp_type))
            throw std::invalid_argument("Adapter type is already registered");
        auto entry = std::make_shared<Entry<T>>(std::move(adapter));
        by_saved_type_.emplace(saved_type, std::move(entry));
        try { saved_type_by_cpp_.emplace(cpp_type, saved_type); }
        catch (...) { by_saved_type_.erase(saved_type); throw; }
    }

    bool contains(const std::string& saved_type) const noexcept {
        return by_saved_type_.count(saved_type) != 0;
    }

    bool can_decode(const Item& item) const noexcept {
        return !item.adapter_type().empty() && contains(item.adapter_type());
    }

    const std::type_info& cpp_type(const Item& item) const {
        return entry_for(item).cpp_type();
    }

    // Creates a typed Item, adds it, and returns its new technical ID.
    template<class T>
    ItemId add(Storage& storage, const T& value, Parameters properties = {}) const {
        const auto& saved_type = saved_type_for<T>();
        const auto& entry = typed_entry<T>(saved_type);
        Item item = entry.adapter.to_item(value, std::move(properties));
        item.adapter_type_ = saved_type;
        if (storage.add(item) != AddResult::added)
            throw std::logic_error("New adapted item has a duplicate ID");
        return item.id();
    }

    // Selects by the saved type first, then verifies the requested C++ type.
    template<class T>
    T get(const Item& item) const {
        return typed_entry<T>(item.adapter_type()).adapter.from_item(item);
    }

    template<class T>
    T get(const Storage& storage, ItemId id) const {
        const auto item = storage.item(id);
        if (!item) throw std::out_of_range("Item was not found");
        return get<T>(*item);
    }

    template<class T>
    bool update(Storage& storage, ItemId id, const T& value) const {
        const auto item = storage.item(id);
        if (!item) return false;
        return typed_entry<T>(item->adapter_type()).adapter.update(storage, id, value);
    }

    // Tags an existing untyped/legacy entry after the game identifies its class.
    template<class T>
    bool bind(Storage& storage, ItemId id) const {
        return storage.set_item_adapter_type(id, saved_type_for<T>());
    }

private:
    struct EntryBase {
        virtual ~EntryBase() = default;
        virtual const std::type_info& cpp_type() const noexcept = 0;
    };
    template<class T>
    struct Entry final : EntryBase {
        explicit Entry(ItemAdapter<T> value) : adapter(std::move(value)) {}
        const std::type_info& cpp_type() const noexcept override { return typeid(T); }
        ItemAdapter<T> adapter;
    };

    const EntryBase& entry_for(const Item& item) const {
        if (item.adapter_type().empty()) throw std::invalid_argument("Item has no saved adapter type");
        const auto found = by_saved_type_.find(item.adapter_type());
        if (found == by_saved_type_.end()) throw std::out_of_range("No adapter is registered for the saved type");
        return *found->second;
    }

    template<class T>
    const std::string& saved_type_for() const {
        const auto found = saved_type_by_cpp_.find(std::type_index(typeid(T)));
        if (found == saved_type_by_cpp_.end()) throw std::out_of_range("No adapter is registered for the C++ type");
        return found->second;
    }

    template<class T>
    const Entry<T>& typed_entry(const std::string& saved_type) const {
        if (saved_type.empty()) throw std::invalid_argument("Item has no saved adapter type");
        const auto found = by_saved_type_.find(saved_type);
        if (found == by_saved_type_.end()) throw std::out_of_range("No adapter is registered for the saved type");
        const auto* typed = dynamic_cast<const Entry<T>*>(found->second.get());
        if (!typed) throw std::invalid_argument("Saved adapter type does not match the requested C++ type");
        return *typed;
    }

    std::map<std::string, std::shared_ptr<EntryBase>> by_saved_type_;
    std::map<std::type_index, std::string> saved_type_by_cpp_;
};

} // namespace game_storage
