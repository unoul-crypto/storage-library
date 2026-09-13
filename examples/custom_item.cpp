#include <game_storage/item_adapter.hpp>
#include <iostream>

using namespace game_storage;

struct Sword {
    std::string name;
    std::int64_t durability;
};

int main() {
    ItemAdapter<Sword> adapter{
        [](const Sword& sword, Parameters& data, Parameters&) {
            data["type"] = "sword";
            data["name"] = sword.name;
            data["durability"] = sword.durability;
        },
        [](const Parameters& data, const Parameters&) {
            if (data.at("type") != Value("sword")) throw std::invalid_argument("Expected a sword");
            return Sword{data.at("name").as<std::string>(), data.at("durability").as<std::int64_t>()};
        }
    };

    Storage chest;
    const auto entry = adapter.to_item(Sword{"Iron sword", 100}, {{"quantity", 1}});
    chest.add(entry);
    auto sword = adapter.from_item(*chest.item(entry.id()));
    sword.durability = 80;
    adapter.update(chest, entry.id(), sword);

    Storage restored;
    restored.load_json(chest.to_json());
    const auto loaded = adapter.from_item(*restored.item(entry.id()));
    std::cout << loaded.name << ": " << loaded.durability << '\n';
    return loaded.durability == 80 ? 0 : 1;
}
