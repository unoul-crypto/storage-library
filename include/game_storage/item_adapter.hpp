#pragma once

#include "storage.hpp"
#include <stdexcept>

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

} // namespace game_storage
