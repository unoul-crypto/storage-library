#include <game_storage/storage.hpp>

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace game_storage;

namespace {
int checks = 0;
void check(bool condition, const char* description) {
    ++checks;
    if (!condition) throw std::runtime_error(description);
}

void storage_operations() {
    Storage storage({{"capacity", 0}, {"locked", true}});
    Item first({{"type", "sword"}, {"durability", 100}});
    Item second({{"type", "sword"}});
    check(first.id() != second.id(), "New instances must have distinct IDs");
    check(storage.add(first) == AddResult::added, "Game parameters must not block add");
    check(storage.add(first) == AddResult::duplicate_id, "Duplicate identity must be rejected");
    check(storage.add(second) == AddResult::added, "Same type must not merge");
    check(storage.size() == 2, "Two separate items must remain");
    check(storage.items()[0].id() == first.id(), "Insertion order must be stable");
    check(!storage.extract(0), "Missing extraction must return empty");
    check(storage.size() == 2, "Failed extraction must not mutate storage");
    const auto extracted = storage.extract(first.id());
    check(extracted && extracted->id() == first.id(), "Extraction must preserve identity");
    check(extracted->parameter("durability") == std::optional<Value>{100}, "Extraction must preserve data");
    check(!storage.contains(first.id()), "Extracted item must leave storage");
    check(storage.items().front().id() == second.id(), "Remaining order must be preserved");
    check(storage.add(*extracted) == AddResult::added, "Extracted item must be reusable");
}

void parameters_and_snapshots() {
    Parameters nested{{"stats", Value::Object{{"damage", 8}}},
                      {"tags", Value::Array{"rare", "weapon"}},
                      {"empty", nullptr}, {"weight", 1.5}};
    Item source(nested);
    Storage storage(nested);
    storage.add(source);
    source.set_parameter("stats", 0);
    check(storage.item(source.id())->parameter("stats")->as<Value::Object>().at("damage") == Value(8),
          "Add must own its parameter data");
    auto list = storage.items();
    auto parameters = list.front().parameters();
    parameters.at("stats").as<Value::Object>()["damage"] = 999;
    list.front().set_parameter("stats", parameters.at("stats"));
    auto value = storage.item(source.id())->parameter("tags");
    value->as<Value::Array>()[0] = "changed";
    check(storage.item(source.id())->parameter("stats")->as<Value::Object>().at("damage") == Value(8),
          "Nested item snapshot must be independent");
    check(storage.item(source.id())->parameter("tags")->as<Value::Array>()[0] == Value("rare"),
          "Nested array snapshot must be independent");
    auto storage_copy = storage.parameters();
    storage_copy.at("stats").as<Value::Object>()["damage"] = 999;
    check(storage.parameter("stats")->as<Value::Object>().at("damage") == Value(8),
          "Storage snapshot must be independent");
    check(storage.parameter("empty").has_value(), "Null parameter must exist");
    check(!storage.parameter("absent"), "Missing must differ from null");
    storage.set_parameter("label", "Chest");
    storage.set_parameter("label", "Bag");
    check(storage.parameter("label") == std::optional<Value>{"Bag"}, "Storage parameter overwrite");
    check(storage.remove_parameter("label"), "Storage parameter removal");
    check(!storage.remove_parameter("label"), "Missing parameter removal");
    check(storage.set_item_parameter(source.id(), "durability", 25), "Stored item update");
    check(storage.item(source.id())->parameter("durability") == std::optional<Value>{25}, "Item update visible");
    check(storage.remove_item_parameter(source.id(), "durability"), "Stored item parameter removal");
    check(!storage.remove_item_parameter(source.id(), "durability"), "Missing item parameter removal");
    check(!storage.set_item_parameter(0, "x", 1), "Missing item update must fail");
    check(!storage.remove_item_parameter(0, "x"), "Missing item removal must fail");
    check(!storage.item(0), "Missing item lookup");
    storage.set_item_parameter(source.id(), "id", "custom metadata");
    check(storage.item(source.id())->id() == source.id(), "Metadata cannot overwrite technical identity");
}

void transfers() {
    Storage a, b;
    Item item({{"type", "key"}});
    a.add(item);
    check(a.transfer_to(item.id(), a) == TransferResult::same_storage, "Self-transfer is a no-op");
    check(a.size() == 1, "Self-transfer preserves source");
    check(a.transfer_to(0, b) == TransferResult::item_not_found, "Missing transfer must fail");
    b.add(item);
    check(a.transfer_to(item.id(), b) == TransferResult::duplicate_id, "Conflicting transfer must fail");
    check(a.contains(item.id()) && b.size() == 1, "Conflict must preserve both storages");
    b.extract(item.id());
    check(a.transfer_to(item.id(), b) == TransferResult::transferred, "Valid transfer must succeed");
    check(a.size() == 0 && b.size() == 1, "Transfer must move exactly one item");
    check(b.item(item.id())->parameters() == item.parameters(), "Transfer must preserve parameters");
}

void actions() {
    Storage storage;
    Item item({{"usable", true}});
    storage.add(item);
    check(storage.actions(item.id()).actions.empty(), "Unset provider yields empty catalog");
    check(storage.actions(0).status == ActionStatus::item_not_found, "Missing item action list");
    check(storage.execute_action(item.id(), "use").status == ActionStatus::action_not_found, "Unset provider execution");
    check(storage.execute_action(0, "use").status == ActionStatus::item_not_found, "Missing item execution");

    bool external_permission = true;
    int executions = 0;
    storage.set_action_provider([&](const Item& current, const Storage& owner, const Context& context) {
        const bool usable = current.parameter("usable") == std::optional<Value>{true};
        const bool unlocked = owner.parameter("locked") != std::optional<Value>{true};
        const auto it = context.find("alive");
        const bool alive = it != context.end() && it->second == Value(true);
        return std::vector<Action>{
            {{"use", usable && unlocked && alive && external_permission, "Unavailable"},
             [&](Storage& target, ItemId id, const Context&) {
                 ++executions;
                 target.set_item_parameter(id, "used", true);
             }}
        };
    });
    const Context context{{"alive", true}};
    check(storage.actions(item.id(), context).actions.front().enabled, "Initial action availability");
    external_permission = false;
    const auto disabled = storage.execute_action(item.id(), "use", context);
    check(disabled.status == ActionStatus::disabled && disabled.reason == "Unavailable", "Revalidate external state");
    check(executions == 0, "Disabled action must not execute");
    external_permission = true;
    storage.set_parameter("locked", true);
    check(!storage.actions(item.id(), context).actions.front().enabled, "Provider sees storage parameters");
    storage.set_parameter("locked", false);
    storage.set_item_parameter(item.id(), "usable", false);
    check(storage.execute_action(item.id(), "use", context).status == ActionStatus::disabled, "Provider sees updated item");
    storage.set_item_parameter(item.id(), "usable", true);
    check(storage.execute_action(item.id(), "use", {{"alive", false}}).status == ActionStatus::disabled, "Provider sees context");
    check(storage.execute_action(item.id(), "missing", context).status == ActionStatus::action_not_found, "Unknown action");
    check(storage.execute_action(item.id(), "use", context).status == ActionStatus::executed, "Available action executes");
    check(executions == 1 && storage.item(item.id())->parameter("used") == std::optional<Value>{true}, "Handler can mutate storage");

    storage.set_action_provider([](const Item&, const Storage&, const Context&) {
        return std::vector<Action>{{{"same", true, {}}, {}}, {{"same", true, {}}, {}}};
    });
    check(storage.actions(item.id()).status == ActionStatus::invalid_catalog, "Duplicate action IDs rejected");
    check(storage.execute_action(item.id(), "same").status == ActionStatus::invalid_catalog, "Ambiguous action cannot execute");
    storage.set_action_provider([](const Item&, const Storage&, const Context&) {
        return std::vector<Action>{{{"", true, {}}, {}}};
    });
    check(storage.actions(item.id()).status == ActionStatus::invalid_catalog, "Empty action ID rejected");
    storage.set_action_provider([](const Item&, const Storage&, const Context&) {
        return std::vector<Action>{{{"empty", true, {}}, {}}};
    });
    check(storage.execute_action(item.id(), "empty").status == ActionStatus::missing_handler, "Missing handler reported");
    storage.set_action_provider([](const Item&, const Storage&, const Context&) {
        return std::vector<Action>{{{"consume", true, {}},
            [](Storage& target, ItemId id, const Context&) {
                target.set_action_provider({});
                target.extract(id);
            }}};
    });
    check(storage.execute_action(item.id(), "consume").status == ActionStatus::executed, "Handler can remove item and provider");
    check(storage.size() == 0, "Consumed item removed");
}

void callback_errors() {
    Storage storage;
    Item item;
    storage.add(item);
    storage.set_action_provider([](const Item&, const Storage&, const Context&) -> std::vector<Action> {
        throw std::runtime_error("provider failure");
    });
    bool caught = false;
    try { storage.actions(item.id()); }
    catch (const std::runtime_error&) { caught = true; }
    check(caught && storage.contains(item.id()), "Provider exception propagates without core mutation");
    storage.set_action_provider([](const Item&, const Storage&, const Context&) {
        return std::vector<Action>{{{"throw", true, {}},
            [](Storage&, ItemId, const Context&) { throw std::runtime_error("handler failure"); }}};
    });
    caught = false;
    try { storage.execute_action(item.id(), "throw"); }
    catch (const std::runtime_error&) { caught = true; }
    check(caught, "Handler exception propagates to game");
}

void json_snapshots() {
    Storage original({{"label", "Saved chest"}, {"empty", nullptr},
                      {"nested", Value::Object{{"array", Value::Array{true, 4, 1.25}}}}});
    Item first({{"name", "Potion"}, {"description", "A quote: \" and a newline:\n"},
                {"fraction", 1.0}, {"zero", -0.0}});
    Item second({{"name", "Gem"}, {"tags", Value::Array{"rare", nullptr}}});
    original.add(first);
    original.add(second);
    const auto json = original.to_json();
    check(json.find("\"version\":1") != std::string::npos, "Snapshot declares a version");
    check(json.find("\"id\":\"") != std::string::npos, "IDs are JSON strings");
    check(json.find("\\n") != std::string::npos, "Control characters are escaped");
    Storage restored({{"old", true}});
    restored.add(Item());
    int provider_calls = 0;
    restored.set_action_provider([&](const Item&, const Storage&, const Context&) {
        ++provider_calls;
        return std::vector<Action>{};
    });
    restored.load_json(json);
    check(restored.to_json() == json, "Snapshot round-trips exactly");
    check(restored.items()[0].id() == first.id() && restored.items()[1].id() == second.id(), "IDs and order survive load");
    check(restored.items()[0].parameter("fraction")->as<double>() == 1.0, "Double remains double");
    check(restored.items()[0].parameter("zero")->as<double>() == 0.0, "Negative zero remains double");
    check(restored.parameter("nested")->as<Value::Object>().at("array").as<Value::Array>()[1] == Value(4),
          "Nested storage values survive load");
    check(!restored.parameter("old"), "Load replaces old parameters");
    restored.actions(first.id());
    check(provider_calls == 1, "Runtime action provider remains attached");
    restored.extract(first.id());
    check(restored.items()[0].id() == second.id(), "Restored items remain operable");

    const auto before = restored.to_json();
    const std::vector<std::string> invalid = {
        "", "{", "[]", json + " trailing", "{\"version\":2,\"parameters\":{},\"items\":[]}",
        "{\"version\":1,\"parameters\":{},\"items\":[{\"id\":\"0\",\"parameters\":{}}]}",
        "{\"version\":1,\"parameters\":{},\"items\":[{\"id\":\"01\",\"parameters\":{}}]}",
        "{\"version\":1,\"parameters\":{},\"items\":[{\"id\":\"1\",\"parameters\":{}},{\"id\":\"1\",\"parameters\":{}}]}",
        "{\"version\":1,\"parameters\":{},\"items\":[{\"id\":\"18446744073709551616\",\"parameters\":{}}]}",
        "{\"version\":1,\"parameters\":{\"x\":01},\"items\":[]}",
        "{\"version\":1,\"parameters\":{\"x\":1e9999},\"items\":[]}",
        "{\"version\":1,\"parameters\":{\"x\":\"\\ud800\"},\"items\":[]}",
        "{\"version\":1,\"parameters\":{\"x\":true,\"x\":false},\"items\":[]}",
        "{\"version\":1,\"parameters\":{},\"items\":[],\"extra\":1}"
    };
    for (const auto& bad : invalid) {
        bool threw = false;
        try { restored.load_json(bad); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "Invalid JSON must be rejected");
        check(restored.to_json() == before, "Failed load must preserve storage");
    }
    original.set_parameter("infinite", std::numeric_limits<double>::infinity());
    bool threw = false;
    try { original.to_json(); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "Non-finite doubles cannot be serialized");
    original.remove_parameter("infinite");
    original.set_parameter("bad_utf8", std::string(1, static_cast<char>(0xff)));
    threw = false;
    try { original.to_json(); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "Invalid UTF-8 cannot be serialized");

    Storage large_id;
    large_id.load_json("{\"version\":1,\"parameters\":{\"symbol\":\"\\u20ac\"},\"items\":[{\"id\":\"9007199254740993\",\"parameters\":{\"n\":-9223372036854775808}}]}");
    check(large_id.items()[0].id() == 9007199254740993ULL, "IDs above JavaScript safe integer survive");
    check(large_id.item(9007199254740993ULL)->parameter("n") == std::optional<Value>{std::numeric_limits<std::int64_t>::min()},
          "Minimum signed integer survives");
    check(large_id.parameter("symbol") == std::optional<Value>{std::string("\xe2\x82\xac")}, "Unicode escape decoded");
    Item later;
    check(later.id() > 9007199254740993ULL, "New IDs advance beyond restored IDs");
    check(large_id.to_json().find("9007199254740993") != std::string::npos, "Large ID serializes exactly");
}
} // namespace

int main() {
    try {
        storage_operations();
        parameters_and_snapshots();
        transfers();
        actions();
        callback_errors();
        json_snapshots();
        std::cout << "Passed " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Check " << checks << " failed: " << error.what() << '\n';
        return 1;
    }
}
