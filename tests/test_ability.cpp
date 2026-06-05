#include <commons/ability.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <concepts>
#include <format>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using comms::AbilityPtr;
using comms::GenericAbility;
using comms::GlobalAbilityRegistry;
using comms::IAbility;
using comms::make_ability;
using comms::RecordPermissionAbility;
using comms::RoleAbility;
using comms::UnknownAbility;

// A custom ability defined + registered via the one-line macro.
COMMONS_DEFINE_ABILITY(ScopeAbility, "scope");

// A custom IAbility sub-interface — the way a refinement is layered — used to
// exercise the registry's generic kinds_of<Base>() filter.
class IScopedAbility : public IAbility {
protected:
    IScopedAbility() = default;
    IScopedAbility(const IScopedAbility&) = default;
    IScopedAbility(IScopedAbility&&) = default;
    IScopedAbility& operator=(const IScopedAbility&) = default;
    IScopedAbility& operator=(IScopedAbility&&) = default;
};

class TenantAbility final : public comms::AbilityKind<"tenant", TenantAbility, IScopedAbility> {
public:
    using comms::AbilityKind<"tenant", TenantAbility, IScopedAbility>::AbilityKind;
};
COMMONS_REGISTER_ABILITY(TenantAbility);

// -- kind / built-ins --------------------------------------------------------

TEST(Ability, KindPerType) {
    EXPECT_EQ(GenericAbility{}.kind(), "generic");
    EXPECT_EQ(UnknownAbility{}.kind(), "unknown");
    EXPECT_EQ(RoleAbility{}.kind(), "role");
    EXPECT_EQ(RecordPermissionAbility{}.kind(), "record_permission");
    EXPECT_EQ(RoleAbility::KIND, "role");
}

TEST(Ability, BuiltinsRegistered) {
    const auto& reg = GlobalAbilityRegistry::instance();
    EXPECT_TRUE(reg.contains("generic"));
    EXPECT_TRUE(reg.contains("unknown"));
    EXPECT_TRUE(reg.contains("role"));
    EXPECT_TRUE(reg.contains("record_permission"));
    EXPECT_FALSE(reg.contains("no_such_kind"));
}

TEST(Ability, ConstructorSetsValue) {
    const RoleAbility r{"admin"};
    EXPECT_EQ(r.value, "admin");
}

// -- default allowed (same kind + value) -------------------------------------

TEST(Ability, DefaultAllowedSameKindAndValue) {
    const RoleAbility required{"admin"};
    const RoleAbility same{"admin"};
    const RoleAbility different{"editor"};

    EXPECT_TRUE(required.allowed(same));
    EXPECT_FALSE(required.allowed(different));
}

TEST(Ability, DefaultAllowedDifferentKind) {
    const RoleAbility role{"admin"};
    const GenericAbility generic{"admin"};
    // Same value, different kind → not allowed.
    EXPECT_FALSE(role.allowed(generic));
}

// -- RecordPermissionAbility (extensible kind) -------------------------------

TEST(Ability, RecordPermissionActionAndResourceMatch) {
    const RecordPermissionAbility required{"read", "order"};
    EXPECT_TRUE(required.allowed(RecordPermissionAbility{"read", "order"}));
    EXPECT_FALSE(required.allowed(RecordPermissionAbility{"write", "order"}));
    EXPECT_FALSE(required.allowed(RecordPermissionAbility{"read", "tenant"}));
}

TEST(Ability, RecordPermissionWildcard) {
    const RecordPermissionAbility required{"read", "order"};
    // A candidate holding "*" satisfies a specific requirement.
    EXPECT_TRUE(required.allowed(RecordPermissionAbility{"*", "order"}));
    EXPECT_TRUE(required.allowed(RecordPermissionAbility{"read", "*"}));
    EXPECT_TRUE(required.allowed(RecordPermissionAbility{"*", "*"}));

    // A wildcard *requirement* is satisfied by anything in that field.
    const RecordPermissionAbility any_action{"*", "order"};
    EXPECT_TRUE(any_action.allowed(RecordPermissionAbility{"delete", "order"}));
}

// -- equals ------------------------------------------------------------------

TEST(Ability, Equals) {
    EXPECT_TRUE(RoleAbility{"admin"}.equals(RoleAbility{"admin"}));
    EXPECT_FALSE(RoleAbility{"admin"}.equals(RoleAbility{"editor"}));
    EXPECT_FALSE(RoleAbility{"admin"}.equals(GenericAbility{"admin"}));

    // RecordPermissionAbility compares its extra fields too. (Extra parens: the
    // brace-init commas would otherwise split the macro arguments.)
    EXPECT_TRUE((
        RecordPermissionAbility{"read", "order"}.equals(RecordPermissionAbility{"read", "order"})));
    EXPECT_FALSE((RecordPermissionAbility{"read", "order"}.equals(
        RecordPermissionAbility{"read", "tenant"})));
}

TEST(Ability, AbilityEqualNullSafe) {
    const AbilityPtr a = make_ability<RoleAbility>("admin");
    const AbilityPtr b = make_ability<RoleAbility>("admin");
    constexpr AbilityPtr none;

    EXPECT_TRUE(comms::ability_equal(a, b));
    EXPECT_TRUE(comms::ability_equal(none, AbilityPtr{}));
    EXPECT_FALSE(comms::ability_equal(a, none));
}

// -- clone -------------------------------------------------------------------

TEST(Ability, CloneIsIndependentCopy) {
    const auto a = make_ability<RecordPermissionAbility>("read", "order");
    const auto copy = a->clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_TRUE(comms::ability_equal(a, copy));

    // Mutating the original does not affect the clone.
    dynamic_cast<RecordPermissionAbility&>(*a).action = "write";
    EXPECT_FALSE(comms::ability_equal(a, copy));
}

// -- factory / registry ------------------------------------------------------

TEST(Ability, MakeAndCreate) {
    const auto a = make_ability<RoleAbility>("admin");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->kind(), "role");

    const auto created = GlobalAbilityRegistry::instance().create("record_permission");
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->kind(), "record_permission");
    EXPECT_EQ(GlobalAbilityRegistry::instance().create("no_such_kind"), nullptr);
}

TEST(Ability, KindsOfSubInterface) {
    const auto scoped = GlobalAbilityRegistry::instance().kinds_of<IScopedAbility>();
    EXPECT_NE(std::ranges::find(scoped, "tenant"), scoped.end());
    EXPECT_EQ(std::ranges::find(scoped, "role"), scoped.end());

    const auto all = GlobalAbilityRegistry::instance().kinds_of<IAbility>();
    EXPECT_NE(std::ranges::find(all, "role"), all.end());
    EXPECT_NE(std::ranges::find(all, "tenant"), all.end());
}

// -- id helper ---------------------------------------------------------------

TEST(Ability, SetIdCapturesString) {
    RoleAbility r{"admin"};
    r.set_id(comms::Uint64Id<struct PermTag>{7U});
    ASSERT_TRUE(r.id.has_value());
    EXPECT_EQ(*r.id, "7");
}

// -- text output -------------------------------------------------------------

TEST(Ability, TitleAndToString) {
    const RoleAbility r{"admin"};
    EXPECT_EQ(r.title(), "RoleAbility");
    EXPECT_EQ(comms::to_string(r), "RoleAbility: admin");

    std::ostringstream os;
    os << r;
    EXPECT_EQ(os.str(), "RoleAbility: admin");

    EXPECT_EQ(std::format("{}", r), "RoleAbility: admin");
}

static_assert(std::derived_from<RoleAbility, IAbility>);
static_assert(std::derived_from<TenantAbility, IScopedAbility>);

}  // namespace
