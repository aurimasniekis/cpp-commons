#include <commons/ability.hpp>
#include <commons/identity.hpp>

#include <gtest/gtest.h>

#include <concepts>
#include <format>
#include <memory>
#include <sstream>
#include <string>

namespace {

using comms::ApiClientIdentity;
using comms::GlobalIdentityRegistry;
using comms::IIdentity;
using comms::make_ability;
using comms::make_identity;
using comms::NoIdentity;
using comms::RecordPermissionAbility;
using comms::RoleAbility;
using comms::RootIdentity;
using comms::ServerIdentity;
using comms::UnknownIdentity;
using comms::UserIdentity;

// A custom identity via the one-line macro.
COMMONS_DEFINE_IDENTITY(ServiceIdentity, "service");

// -- kind / built-ins --------------------------------------------------------

TEST(Identity, KindPerType) {
    EXPECT_EQ(UserIdentity{}.kind(), "user");
    EXPECT_EQ(ServerIdentity{}.kind(), "server");
    EXPECT_EQ(ApiClientIdentity{}.kind(), "api_client");
    EXPECT_EQ(UnknownIdentity{}.kind(), "unknown");
    EXPECT_EQ(RootIdentity{}.kind(), "root");
    EXPECT_EQ(NoIdentity{}.kind(), "none");
}

TEST(Identity, BuiltinsRegistered) {
    const auto& reg = GlobalIdentityRegistry::instance();
    for (const auto* kind : {"user", "server", "api_client", "unknown", "root", "none"}) {
        EXPECT_TRUE(reg.contains(kind)) << kind;
    }
    EXPECT_FALSE(reg.contains("no_such_kind"));
}

TEST(Identity, ConstructorSetsValue) {
    const UserIdentity u{"alice"};
    EXPECT_EQ(u.value, "alice");
}

// -- helpers -----------------------------------------------------------------

TEST(Identity, SetIdAndAddAbility) {
    UserIdentity u{"alice"};
    u.set_id(comms::Uint64Id<struct UserTag>{42U});
    ASSERT_TRUE(u.id.has_value());
    EXPECT_EQ(*u.id, "42");

    u.add_ability(make_ability<RoleAbility>("admin"));
    ASSERT_EQ(u.abilities.size(), 1U);
    EXPECT_EQ(u.abilities[0]->value, "admin");
}

// -- satisfies / allowed (the two equivalent spellings) ----------------------

TEST(Identity, SatisfiesOverHeldAbilities) {
    auto user = make_identity<UserIdentity>("alice");
    user->add_ability(make_ability<RoleAbility>("editor"));
    user->add_ability(make_ability<RecordPermissionAbility>("read", "order"));

    const RoleAbility editor_required{"editor"};
    const RoleAbility admin_required{"admin"};

    // identity.satisfies(required) and required.allowed(identity) are equivalent.
    EXPECT_TRUE(user->satisfies(editor_required));
    EXPECT_TRUE(editor_required.allowed(*user));

    EXPECT_FALSE(user->satisfies(admin_required));
    EXPECT_FALSE(admin_required.allowed(*user));

    const RecordPermissionAbility read_order{"read", "order"};
    EXPECT_TRUE(read_order.allowed(*user));
    const RecordPermissionAbility write_order{"write", "order"};
    EXPECT_FALSE(write_order.allowed(*user));
}

TEST(Identity, RootAllowsEverything) {
    const auto root = make_identity<RootIdentity>();
    EXPECT_TRUE(root->satisfies(RoleAbility{"anything"}));
    EXPECT_TRUE(RoleAbility{"admin"}.allowed(*root));
    // Extra parens: the brace-init comma would otherwise split the macro args.
    EXPECT_TRUE((RecordPermissionAbility{"delete", "everything"}.allowed(*root)));
}

TEST(Identity, NoIdentityAllowsNothing) {
    const auto none = make_identity<NoIdentity>();
    EXPECT_FALSE(none->satisfies(RoleAbility{"anything"}));
    EXPECT_FALSE(RoleAbility{"admin"}.allowed(*none));

    // Even after granting an ability, NoIdentity overrides satisfies to false.
    none->add_ability(make_ability<RoleAbility>("admin"));
    EXPECT_FALSE(RoleAbility{"admin"}.allowed(*none));
}

// -- clone deep-copies abilities ---------------------------------------------

TEST(Identity, CloneDeepCopiesAbilities) {
    const auto user = make_identity<UserIdentity>("alice");
    user->add_ability(make_ability<RoleAbility>("editor"));

    const auto copy = user->clone();
    ASSERT_NE(copy, nullptr);
    ASSERT_EQ(copy->abilities.size(), 1U);
    EXPECT_TRUE(comms::identity_equal(user, copy));

    // The cloned ability is a distinct object.
    EXPECT_NE(copy->abilities[0].get(), user->abilities[0].get());

    // Mutating the original's ability does not affect the clone.
    user->abilities[0]->value = "viewer";
    EXPECT_FALSE(comms::identity_equal(user, copy));
}

// -- equals / identity_equal -------------------------------------------------

TEST(Identity, Equals) {
    const auto a = make_identity<UserIdentity>("alice");
    const auto b = make_identity<UserIdentity>("alice");
    EXPECT_TRUE(comms::identity_equal(a, b));

    const auto c = make_identity<UserIdentity>("bob");
    EXPECT_FALSE(comms::identity_equal(a, c));

    const auto server = make_identity<ServerIdentity>("alice");
    EXPECT_FALSE(comms::identity_equal(a, server));  // different kind

    // Abilities participate in equality.
    a->add_ability(make_ability<RoleAbility>("admin"));
    EXPECT_FALSE(comms::identity_equal(a, b));
    b->add_ability(make_ability<RoleAbility>("admin"));
    EXPECT_TRUE(comms::identity_equal(a, b));
}

TEST(Identity, IdentityEqualNullSafe) {
    const auto a = make_identity<UserIdentity>("alice");
    constexpr comms::IdentityPtr none;
    EXPECT_TRUE(comms::identity_equal(none, comms::IdentityPtr{}));
    EXPECT_FALSE(comms::identity_equal(a, none));
}

// -- text output -------------------------------------------------------------

TEST(Identity, TitleAndToString) {
    const UserIdentity u{"alice"};
    EXPECT_EQ(u.title(), "UserIdentity");
    EXPECT_EQ(comms::to_string(u), "UserIdentity: alice");

    std::ostringstream os;
    os << u;
    EXPECT_EQ(os.str(), "UserIdentity: alice");

    EXPECT_EQ(std::format("{}", u), "UserIdentity: alice");
}

static_assert(std::derived_from<UserIdentity, IIdentity>);
static_assert(std::derived_from<ServiceIdentity, IIdentity>);

}  // namespace
