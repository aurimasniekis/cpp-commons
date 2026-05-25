// Tour of comms::Prioritized: a PrioritizedBuilder-based fluent adapter, the
// WithPriority wrapper in both flavors (inheritance for a class, composition for
// a fundamental), a PrioritizedSet that iterates in ascending-priority order with
// insertion order breaking ties, and the shared_ptr comparators. JSON-free, so it
// builds in the base config.

#include <commons/prioritized.hpp>

#include <iostream>
#include <memory>
#include <set>
#include <string>

namespace {

// An adapter that carries a priority via the CRTP builder mixin. The fluent
// setters return Adapter&, so calls chain.
struct Adapter : comms::PrioritizedBuilder<Adapter> {
    std::string name;
    explicit Adapter(std::string n) : name(std::move(n)) {}
};

// A plain transport used to show the WithPriority inheritance flavor: because
// Transport is a non-final class, WithPriority<Transport> *is a* Transport.
struct Transport {
    std::string scheme;
    [[nodiscard]] std::string describe() const {
        return "transport:" + scheme;
    }
};

}  // namespace

int main() {
    namespace c = comms;

    // 1) Builder: fluent priority setters returning the derived type.
    Adapter fast("fast");
    Adapter slow("slow");
    fast.highest_priority();
    slow.priority(100);
    std::cout << fast.name << " priority = " << fast.priority() << "\n";
    std::cout << slow.name << " priority = " << slow.priority() << "\n";

    // 2) WithPriority — inheritance flavor (a true is-a Transport).
    auto http = c::with_priority(10, Transport{"http"});
    std::cout << "\nWithPriority<Transport> is-a Transport: " << http.value().describe()
              << " (priority " << http.priority() << ")\n";

    // WithPriority — composition flavor for a fundamental type.
    auto level = c::with_priority(-5, 42);
    std::cout << "WithPriority<int> value " << *level << " (priority " << level.priority() << ")\n";

    // 3) PrioritizedSet — iterates by (priority asc, insertion order asc).
    c::PrioritizedSet<std::string> pipeline;
    pipeline.insert(5, "compress");
    pipeline.insert(1, "auth");
    pipeline.insert(5, "log");  // ties with "compress": insertion order wins
    pipeline.insert(1, "trace");

    std::cout << "\nPipeline in execution order:\n";
    for (const auto& stage : pipeline) {
        std::cout << "  [" << pipeline.priority_of(stage) << "] " << stage << "\n";
    }

    // Reordering an element re-snapshots its priority.
    pipeline.set_priority("log", 0);
    std::cout << "after set_priority(\"log\", 0), first stage is: " << *pipeline.begin() << "\n";

    // 4) Comparators: order shared_ptrs by priority. LenientPrioritizedCompare
    // works even though Adapter is reached through the base interface.
    std::set<std::shared_ptr<c::Prioritized>, c::PrioritizedCompare> ordered;
    ordered.insert(c::make_prioritized<Transport>(30, Transport{"slow"}));
    ordered.insert(c::make_prioritized<Transport>(20, Transport{"medium"}));
    ordered.insert(c::make_prioritized<Transport>(10, Transport{"fast"}));

    std::cout << "\nshared_ptr<Prioritized> set, by ascending priority:\n";
    for (const auto& p : ordered) {
        std::cout << "  priority " << p->priority() << "\n";
    }

    std::cout << "\nget_priority on a null shared_ptr falls back to DEFAULT: "
              << c::get_priority(std::shared_ptr<c::Prioritized>{}) << "\n";

    return 0;
}
