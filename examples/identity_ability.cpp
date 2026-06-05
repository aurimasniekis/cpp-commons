// Tour of the comms::Identity / comms::Ability families: a shared authentication
// (who) + authorization (what) vocabulary built as polymorphic open sets, just
// like comms::IOrigin / comms::IReason. JSON-free, so it builds in the base
// library.
//
// The check reads `required.allowed(subject)`: the *required* ability is the
// receiver; the argument is the candidate (an ability or a whole identity) that
// must satisfy it. `required.allowed(identity)` is equivalent to
// `identity.satisfies(required)`.

#include <commons/ability.hpp>
#include <commons/identity.hpp>

#include <iostream>

// A custom ability defined + registered in one line (inherits the AbilityKind
// constructors, so make_ability<FeatureFlagAbility>("beta") just works).
COMMONS_DEFINE_ABILITY(FeatureFlagAbility, "feature_flag");

int main() {
    namespace c = comms;

    // -- abilities: role matching --------------------------------------------
    const c::RoleAbility admin_required{"admin"};
    std::cout << "admin.allowed(admin) : " << admin_required.allowed(c::RoleAbility{"admin"})
              << "\n";  // 1
    std::cout << "admin.allowed(editor): " << admin_required.allowed(c::RoleAbility{"editor"})
              << "\n";  // 0

    // -- abilities: record permission with a "*" wildcard --------------------
    const c::RecordPermissionAbility read_order{"read", "order"};
    std::cout << "read/order.allowed(read/order): "
              << read_order.allowed(c::RecordPermissionAbility{"read", "order"}) << "\n";  // 1
    std::cout << "read/order.allowed(*/order)   : "
              << read_order.allowed(c::RecordPermissionAbility{"*", "order"}) << "\n";  // 1

    // -- an identity holding abilities ---------------------------------------
    const auto user = c::make_identity<c::UserIdentity>("alice");
    user->add_ability(c::make_ability<c::RoleAbility>("admin"));
    user->add_ability(c::make_ability<c::RecordPermissionAbility>("read", "order"));
    std::cout << "identity              : " << *user << "\n";  // "UserIdentity: alice"

    // required.allowed(identity) == identity.satisfies(required).
    std::cout << "admin allowed for user: " << admin_required.allowed(*user) << "\n";    // 1
    std::cout << "user satisfies admin  : " << user->satisfies(admin_required) << "\n";  // 1
    std::cout << "editor allowed for user: " << c::RoleAbility{"editor"}.allowed(*user)
              << "\n";  // 0

    // -- Root allows everything; None allows nothing -------------------------
    const auto root = c::make_identity<c::RootIdentity>();
    const auto none = c::make_identity<c::NoIdentity>();
    std::cout << "root allows admin     : " << admin_required.allowed(*root) << "\n";  // 1
    std::cout << "none allows admin     : " << admin_required.allowed(*none) << "\n";  // 0

    // -- the custom one-liner kind -------------------------------------------
    const auto beta = c::make_ability<FeatureFlagAbility>("beta");
    std::cout << "custom ability        : " << *beta << "\n";  // "FeatureFlagAbility: beta"

    return 0;
}
