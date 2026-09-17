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

        ItemAdapterRegistry registry;
        fail = false;
        registry.register_adapter<std::unique_ptr<int>>("example.integer", adapter);
        Storage typed_storage;
        const auto typed_id = registry.add<std::unique_ptr<int>>(
            typed_storage, std::make_unique<int>(41), {{"quantity", 2}});
        check(typed_storage.item(typed_id)->adapter_type() == "example.integer");
        check(registry.contains("example.integer") && registry.can_decode(*typed_storage.item(typed_id)));
        check(registry.cpp_type(*typed_storage.item(typed_id)) == typeid(std::unique_ptr<int>));
        check(*registry.get<std::unique_ptr<int>>(typed_storage, typed_id) == 41);
        check(registry.update(typed_storage, typed_id, std::make_unique<int>(42)));
        check(*registry.get<std::unique_ptr<int>>(*typed_storage.item(typed_id)) == 42);

        Storage typed_restored;
        typed_restored.load_json(typed_storage.to_json());
        check(typed_restored.item(typed_id)->adapter_type() == "example.integer");
        check(*registry.get<std::unique_ptr<int>>(typed_restored, typed_id) == 42);

        threw = false;
        try { registry.get<int>(*typed_restored.item(typed_id)); }
        catch (const std::invalid_argument&) { threw = true; }
        check(threw);
        threw = false;
        try { registry.get<std::unique_ptr<int>>(Item()); }
        catch (const std::invalid_argument&) { threw = true; }
        check(threw);
        ItemAdapterRegistry empty_registry;
        check(!empty_registry.can_decode(*typed_restored.item(typed_id)));
        threw = false;
        try { empty_registry.get<std::unique_ptr<int>>(*typed_restored.item(typed_id)); }
        catch (const std::out_of_range&) { threw = true; }
        check(threw);
        Storage legacy;
        const auto legacy_item = adapter.to_item(std::make_unique<int>(7));
        legacy.add(legacy_item);
        check(!registry.can_decode(*legacy.item(legacy_item.id())));
        check(registry.bind<std::unique_ptr<int>>(legacy, legacy_item.id()));
        check(*registry.get<std::unique_ptr<int>>(legacy, legacy_item.id()) == 7);
        check(!registry.bind<std::unique_ptr<int>>(legacy, 0));
        threw = false;
        try { registry.register_adapter<std::unique_ptr<int>>("other.integer", adapter); }
        catch (const std::invalid_argument&) { threw = true; }
        check(threw);
        threw = false;
        try { registry.register_adapter<int>("example.integer", ItemAdapter<int>{
            [](const int&, Parameters&, Parameters&) {},
            [](const Parameters&, const Parameters&) { return 0; }}); }
        catch (const std::invalid_argument&) { threw = true; }
        check(threw);
        std::cout << "Adapter checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
