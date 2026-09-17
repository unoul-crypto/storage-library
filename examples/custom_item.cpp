#include <game_storage/item_adapter.hpp>
#include <iostream>

using namespace game_storage;

struct Sword {
    std::string name;
    std::int64_t durability;
};

int main() {
    ItemAdapterRegistry adapters;
    adapters.register_adapter<Sword>("demo.sword", ItemAdapter<Sword>{
        [](const Sword& sword, Parameters& data, Parameters&) {
            data["type"] = "sword";
            data["name"] = sword.name;
            data["durability"] = sword.durability;
        },
        [](const Parameters& data, const Parameters&) {
            if (data.at("type") != Value("sword")) throw std::invalid_argument("Expected a sword");
            return Sword{data.at("name").as<std::string>(), data.at("durability").as<std::int64_t>()};
        }
    });

    Storage chest;
    const auto id = adapters.add<Sword>(chest, Sword{"Iron sword", 100}, {{"quantity", 1}});
    auto sword = adapters.get<Sword>(chest, id);
    sword.durability = 80;
    adapters.update(chest, id, sword);

    Storage restored;
    restored.load_json(chest.to_json());
    const auto loaded = adapters.get<Sword>(restored, id);
    std::cout << loaded.name << ": " << loaded.durability << '\n';
    return loaded.durability == 80 ? 0 : 1;
}
