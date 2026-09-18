#include <game_storage/stacks.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace game_storage;
namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
Item stack(std::string type, std::int64_t quantity) {
    return Item(Parameters{{"type", std::move(type)}}, Parameters{{"quantity", quantity}});
}
StackOperations operations() {
    return StackOperations({[](const Item& a, const Item& b) {
        return a.adapter_type() == b.adapter_type() && a.item_data() == b.item_data();
    }});
}

void quantities_and_cloning() {
    auto stacks = operations();
    Item single({{"type", "coin"}});
    check(stacks.quantity(single).status == StackStatus::success && stacks.quantity(single).value == 1,
          "Missing quantity means one");
    single.set_entry_property("quantity", 0);
    check(stacks.quantity(single).status == StackStatus::invalid_quantity, "Zero quantity rejected");
    single.set_entry_property("quantity", 1.0);
    check(stacks.quantity(single).status == StackStatus::invalid_quantity, "Non-integer quantity rejected");
    Item original = stack("coin", 4);
    original.set_item_data_value("nested", Value::Object{{"x", 1}});
    original.set_entry_property("slot", 2);
    Storage storage;
    storage.add(original);
    storage.set_item_adapter_type(original.id(), "game.coin");
    const auto snapshot = *storage.item(original.id());
    const auto clone = snapshot.new_instance();
    check(clone.id() != snapshot.id() && clone.adapter_type() == "game.coin", "Clone gets new ID and keeps adapter type");
    check(clone.item_data() == snapshot.item_data() && clone.entry_properties() == snapshot.entry_properties(),
          "Clone copies both dictionaries");
    check(stacks.quantity(storage, 0).status == StackStatus::item_not_found, "Missing quantity lookup reported");
}

void extraction_and_split() {
    auto stacks = operations();
    Storage storage;
    Item source = stack("arrow", 10);
    storage.add(source);
    storage.set_item_adapter_type(source.id(), "game.arrow");
    auto extracted = stacks.extract_quantity(storage, source.id(), 3);
    check(extracted.status == StackStatus::success && extracted.extracted.has_value(), "Partial extraction returns item");
    check(extracted.extracted->id() != source.id() && extracted.extracted->adapter_type() == "game.arrow",
          "Partial extraction creates typed identity");
    check(stacks.quantity(*extracted.extracted).value == 3 && stacks.quantity(storage, source.id()).value == 7,
          "Partial extraction divides quantity");
    check(storage.size() == 1, "Partial extraction does not insert detached part");
    auto split = stacks.split(storage, source.id(), 2);
    check(split.status == StackStatus::success && storage.size() == 2, "Split inserts second entry");
    check(stacks.quantity(storage, source.id()).value == 5 && stacks.quantity(storage, split.item_id).value == 2,
          "Split preserves total quantity");
    check(stacks.split(storage, source.id(), 5).status == StackStatus::not_partial, "Whole stack cannot be split");
    check(stacks.extract_quantity(storage, source.id(), 6).status == StackStatus::insufficient_quantity,
          "Excess extraction rejected");
    check(stacks.extract_quantity(storage, source.id(), 0).status == StackStatus::invalid_amount,
          "Nonpositive extraction rejected");
    const auto full = stacks.extract_quantity(storage, source.id(), 5);
    check(full.status == StackStatus::success && full.extracted->id() == source.id() && !storage.contains(source.id()),
          "Full extraction preserves original identity");
}

void transfers_and_merges() {
    auto stacks = operations();
    Storage source_storage, destination;
    Item arrows = stack("arrow", 8);
    source_storage.add(arrows);
    auto partial = stacks.transfer_quantity_to(source_storage, arrows.id(), 3, destination);
    check(partial.status == StackStatus::success && partial.item_id != arrows.id(), "Partial transfer creates identity");
    check(stacks.quantity(source_storage, arrows.id()).value == 5 &&
          stacks.quantity(destination, partial.item_id).value == 3, "Partial transfer divides quantity");
    auto full = stacks.transfer_quantity_to(source_storage, arrows.id(), 5, destination);
    check(full.status == StackStatus::success && full.item_id == arrows.id() && !source_storage.contains(arrows.id()),
          "Full transfer preserves identity");
    check(stacks.transfer_quantity_to(destination, arrows.id(), 1, destination).status == StackStatus::same_storage,
          "Transfer to same storage rejected");

    auto merged = stacks.merge(destination, partial.item_id, destination, arrows.id());
    check(merged.status == StackStatus::success && merged.item_id == arrows.id(), "Compatible stacks merge");
    check(destination.size() == 1 && stacks.quantity(destination, arrows.id()).value == 8,
          "Merge removes source and adds quantities");
    check(stacks.merge(destination, arrows.id(), destination, arrows.id()).status == StackStatus::same_item,
          "Stack cannot merge into itself");

    Item bolts = stack("bolt", 2);
    destination.add(bolts);
    check(stacks.merge(destination, bolts.id(), destination, arrows.id()).status == StackStatus::incompatible,
          "Developer compatibility callback controls merge");
    check(destination.contains(bolts.id()), "Rejected merge preserves source");
    check(stacks.merge(destination, 0, destination, arrows.id()).status == StackStatus::item_not_found,
          "Missing merge source reported");
    check(stacks.merge(destination, bolts.id(), destination, 0).status == StackStatus::destination_not_found,
          "Missing merge destination reported");

    Storage overflow_storage;
    Item first = stack("gem", std::numeric_limits<std::int64_t>::max());
    Item second = stack("gem", 1);
    overflow_storage.add(first); overflow_storage.add(second);
    check(stacks.merge(overflow_storage, second.id(), overflow_storage, first.id()).status == StackStatus::quantity_overflow,
          "Quantity overflow rejected");
    check(overflow_storage.size() == 2, "Overflow leaves both stacks unchanged");
}

void validation() {
    bool threw = false;
    try { StackOperations invalid({}); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "Compatibility callback required");
    threw = false;
    try { StackOperations invalid({[](const Item&, const Item&) { return true; }, ""}); }
    catch (const std::invalid_argument&) { threw = true; }
    check(threw, "Quantity key required");
}
}

int main() {
    try {
        quantities_and_cloning(); extraction_and_split(); transfers_and_merges(); validation();
        std::cout << "Passed " << checks << " stack checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Check " << checks << " failed: " << error.what() << '\n';
        return 1;
    }
}
