#include <game_storage/item_adapter.hpp>
#include <iostream>
#include <memory>

using namespace game_storage;
void check(bool ok) { if (!ok) throw std::runtime_error("Adapter check failed"); }

int main() {
    try {
        bool fail = false;
        int calls = 0;
        ItemAdapter<std::unique_ptr<int>> adapter{
            [&](const std::unique_ptr<int>& value, Parameters& data, Parameters& properties) {
                ++calls;
                data["value"] = *value;
                properties["encoded"] = true;
                if (fail) throw std::runtime_error("Encoding failed");
            },
            [](const Parameters& data, const Parameters&) {
                return std::make_unique<int>(static_cast<int>(data.at("value").as<std::int64_t>()));
            }
        };
        Storage storage;
        const auto entry = adapter.to_item(std::make_unique<int>(12), {{"quantity", 3}});
        storage.add(entry);
        storage.set_item_data_value(entry.id(), "extra", "retained");
        check(adapter.update(storage, entry.id(), std::make_unique<int>(24)));
        check(storage.size() == 1 && storage.items()[0].id() == entry.id());
        check(storage.item(entry.id())->entry_property("quantity") == std::optional<Value>{3});
        check(storage.item(entry.id())->entry_property("encoded") == std::optional<Value>{true});
        check(storage.item(entry.id())->item_data_value("extra") == std::optional<Value>{"retained"});
        check(*adapter.from_item(*storage.item(entry.id())) == 24);
        const auto before = storage.to_json();
        fail = true;
        bool threw = false;
        try { adapter.update(storage, entry.id(), std::make_unique<int>(99)); }
        catch (const std::runtime_error&) { threw = true; }
        check(threw && storage.to_json() == before);
        const auto previous_calls = calls;
        check(!adapter.update(storage, 0, std::make_unique<int>(99)) && calls == previous_calls);
        Storage restored;
        restored.load_json(before);
        check(*adapter.from_item(*restored.item(entry.id())) == 24);
        threw = false;
        try { adapter.from_item(Item()); } catch (const std::out_of_range&) { threw = true; }
        check(threw);
        threw = false;
        try { ItemAdapter<int> invalid({}, {}); } catch (const std::invalid_argument&) { threw = true; }
        check(threw);
        std::cout << "Adapter checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
