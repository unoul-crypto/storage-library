#include <game_storage/storage.hpp>
#include <iostream>

using namespace game_storage;

int main() {
    Storage chest({{"label", "Wooden chest"}});
    Storage backpack;
    Item potion({{"type", "health_potion"}, {"healing", 25}});
    chest.add(potion);
    chest.transfer_to(potion.id(), backpack);

    backpack.set_action_provider([](const Item& item, const Storage&, const Context& context) {
        if (item.parameter("type") != std::optional<Value>{Value("health_potion")}) {
            return std::vector<Action>{};
        }
        const auto it = context.find("can_drink");
        const bool allowed = it != context.end() && it->second == Value(true);
        return std::vector<Action>{
            {{"drink", allowed, allowed ? "" : "Cannot drink right now"},
             [](Storage& storage, ItemId id, const Context&) {
                 const auto consumed = storage.extract(id);
                 if (consumed) std::cout << "Potion consumed\n";
             }}
        };
    });

    Context context{{"can_drink", false}};
    for (const auto& action : backpack.actions(potion.id(), context).actions) {
        std::cout << action.id << ": " << action.disabled_reason << '\n';
    }
    context["can_drink"] = true;
    const auto result = backpack.execute_action(potion.id(), "drink", context);
    std::cout << "Items remaining: " << backpack.size() << '\n';
    return result.status == ActionStatus::executed && backpack.size() == 0 ? 0 : 1;
}
