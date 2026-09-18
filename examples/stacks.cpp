#include <game_storage/stacks.hpp>
#include <iostream>

using namespace game_storage;

int main() {
    Storage chest, backpack;
    Item potions(Parameters{{"type", "health_potion"}}, Parameters{{"quantity", 10}});
    chest.add(potions);

    StackOperations stacks({[](const Item& a, const Item& b) {
        return a.adapter_type() == b.adapter_type() && a.item_data() == b.item_data();
    }});

    const auto moved = stacks.transfer_quantity_to(chest, potions.id(), 3, backpack);
    const auto left = stacks.quantity(chest, potions.id());
    const auto carried = stacks.quantity(backpack, moved.item_id);
    std::cout << "Chest: " << left.value << ", backpack: " << carried.value << '\n';
    return moved.status == StackStatus::success && left.value == 7 && carried.value == 3 ? 0 : 1;
}
