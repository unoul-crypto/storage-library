#include <game_storage/sync.hpp>

#include <iostream>
#include <stdexcept>

using namespace game_storage;
namespace net = game_storage::sync;
namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void updates_and_guards() {
    Storage before({{"capacity", 20}});
    Item a({{"name", "Potion"}}, {{"quantity", 3}});
    Item b({{"name", "Gem"}});
    before.add(a); before.add(b);
    Storage server, client;
    server.load_json(before.to_json()); client.load_json(before.to_json());
    int provider_calls = 0;
    client.set_action_provider([&](const Item&, const Storage&, const Context&) {
        ++provider_calls; return std::vector<Action>{};
    });
    server.set_parameter("capacity", 25);
    server.set_item_adapter_type(a.id(), "game.potion");
    server.set_item_data_value(a.id(), "name", "Large potion");
    server.set_entry_property(a.id(), "quantity", 4);
    server.extract(b.id());
    Item c({{"name", "Sword"}});
    server.add(c);
    const auto delta = net::make_delta(before, server);
    check(delta.base_revision == before.revision() && delta.target_revision == server.revision(),
          "Delta carries revision range");
    check(delta.removed == std::vector<ItemId>{b.id()} && delta.upserted.size() == 2,
          "Delta contains removed and changed entries only");
    check(delta.parameters && !delta.order, "Parameter change included without redundant order");
    const auto wire = delta.to_json();
    const auto received = net::Delta::from_json(wire);
    check(received.to_json() == wire, "Delta JSON round-trips exactly");
    check(net::apply_delta(client, received) == net::ApplyStatus::applied &&
          client.to_json() == server.to_json(), "Delta reconstructs authoritative snapshot");
    client.actions(a.id());
    check(provider_calls == 1, "Applying delta preserves runtime action provider");
    check(net::apply_delta(client, received) == net::ApplyStatus::revision_mismatch,
          "Duplicate delivery is rejected by revision");

    Storage diverged;
    diverged.load_json(before.to_json());
    diverged.set_item_data_value(a.id(), "name", "Different");
    diverged.load_json("{\"version\":4,\"revision\":\"2\",\"parameters\":{},\"items\":[]}");
    check(diverged.revision() == before.revision() &&
          net::apply_delta(diverged, received) == net::ApplyStatus::state_mismatch,
          "Equal revision on another timeline is rejected by checksum");
    const auto unchanged = diverged.to_json();
    check(diverged.to_json() == unchanged, "Rejected delta preserves current state");

    Storage fresh;
    fresh.load_json(before.to_json());
    auto tampered = received;
    tampered.upserted.front().item_data["name"] = "Altered in transit";
    check(net::apply_delta(fresh, tampered) == net::ApplyStatus::invalid_delta &&
          fresh.to_json() == before.to_json(), "Checksum rejects damaged payload atomically");
    tampered = received;
    tampered.removed.push_back(tampered.removed.front());
    check(net::apply_delta(fresh, tampered) == net::ApplyStatus::invalid_delta,
          "Duplicate operations are rejected");
}

void order_and_empty_delta() {
    Storage before;
    Item a, b;
    before.add(a); before.add(b);
    Storage after, client;
    client.load_json(before.to_json());
    const auto reordered = "{\"version\":4,\"revision\":\"3\",\"parameters\":{},\"items\":["
        "{\"id\":\"" + std::to_string(b.id()) + "\",\"type\":\"\",\"item_data\":{},\"properties\":{}},"
        "{\"id\":\"" + std::to_string(a.id()) + "\",\"type\":\"\",\"item_data\":{},\"properties\":{}}]}";
    after.load_json(reordered);
    const auto delta = net::make_delta(before, after);
    check(delta.order && delta.order->front() == b.id() && delta.upserted.empty(),
          "Reordering sends IDs without resending item content");
    check(net::apply_delta(client, net::Delta::from_json(delta.to_json())) == net::ApplyStatus::applied &&
          client.to_json() == after.to_json(), "Explicit order is applied exactly");

    const auto empty = net::make_delta(before, before);
    check(empty.removed.empty() && empty.upserted.empty() && !empty.parameters && !empty.order,
          "Identical snapshots yield empty delta");
    Storage copy;
    copy.load_json(before.to_json());
    check(net::apply_delta(copy, empty) == net::ApplyStatus::applied &&
          copy.to_json() == before.to_json(), "Empty delta is safe to apply");
    Storage reverted;
    reverted.load_json(before.to_json());
    reverted.set_parameter("temporary", true);
    reverted.remove_parameter("temporary");
    const auto revision_only = net::make_delta(before, reverted);
    check(revision_only.target_revision == before.revision() + 2 &&
          revision_only.removed.empty() && revision_only.upserted.empty() && !revision_only.parameters,
          "Reverted changes produce a revision-only delta");
    check(net::apply_delta(copy, revision_only) == net::ApplyStatus::applied &&
          copy.to_json() == reverted.to_json(), "Revision-only delta advances receiver");
    bool threw = false;
    try { net::make_delta(after, before); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "Reverse revision history is rejected");
}

void json_validation() {
    Storage before;
    Item item; before.add(item);
    Storage after; after.load_json(before.to_json());
    after.set_item_data_value(item.id(), "x", 1);
    const auto wire = net::make_delta(before, after).to_json();
    const std::vector<std::string> invalid = {
        "", "[]", wire + " trailing",
        "{\"version\":2,\"base_revision\":\"0\",\"target_revision\":\"1\",\"base_checksum\":\"0\",\"target_checksum\":\"0\",\"removed\":[],\"upserted\":[],\"parameters\":null,\"order\":null}",
        "{\"version\":1,\"base_revision\":\"01\",\"target_revision\":\"1\",\"base_checksum\":\"0\",\"target_checksum\":\"0\",\"removed\":[],\"upserted\":[],\"parameters\":null,\"order\":null}",
        "{\"version\":1,\"base_revision\":\"1\",\"target_revision\":\"0\",\"base_checksum\":\"0\",\"target_checksum\":\"0\",\"removed\":[],\"upserted\":[],\"parameters\":null,\"order\":null}",
        "{\"version\":1,\"base_revision\":\"0\",\"target_revision\":\"1\",\"base_checksum\":\"0\",\"target_checksum\":\"0\",\"removed\":[\"0\"],\"upserted\":[],\"parameters\":null,\"order\":null}"
    };
    for (const auto& text : invalid) {
        bool threw = false;
        try { net::Delta::from_json(text); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "Malformed delta JSON is rejected");
    }
}
} // namespace

int main() {
    try {
        updates_and_guards(); order_and_empty_delta(); json_validation();
        std::cout << "Passed " << checks << " sync checks\n";
    } catch (const std::exception& error) {
        std::cerr << "Check " << checks << " failed: " << error.what() << '\n';
        return 1;
    }
}
