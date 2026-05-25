#include <commons/flag.hpp>

#include <gtest/gtest.h>

#include <concepts>
#include <string_view>
#include <vector>

namespace {

using comms::AnyFlag;
using comms::AnyFlagCategory;
using comms::Flag;
using comms::FlagCategory;
using comms::FlagInCategory;
using comms::FlagRef;
using comms::FlagSet;
using comms::GlobalFlagRegistry;
using comms::UnsetFlagCategory;

// -- compile-time types ------------------------------------------------------

struct NetworkCategory : FlagCategory<"network"> {};

struct Ipv6 : Flag<"ipv6", NetworkCategory> {};
struct Verbose : Flag<"verbose"> {};  // default UnsetFlagCategory

static_assert(FlagCategory<"network">::name == "network");
static_assert(UnsetFlagCategory::name == "unset");

static_assert(Ipv6::name == "ipv6");
static_assert(Ipv6::category_name == "network");
static_assert(std::same_as<Ipv6::category, NetworkCategory>);

static_assert(Verbose::name == "verbose");
static_assert(Verbose::category_name == "unset");
static_assert(std::same_as<Verbose::category, UnsetFlagCategory>);

// -- concepts ----------------------------------------------------------------

static_assert(AnyFlagCategory<NetworkCategory>);
static_assert(AnyFlagCategory<UnsetFlagCategory>);
static_assert(AnyFlag<Ipv6>);
static_assert(AnyFlag<Verbose>);
static_assert(!AnyFlag<NetworkCategory>);  // a category is not a flag
static_assert(FlagInCategory<Ipv6, NetworkCategory>);
static_assert(!FlagInCategory<Ipv6, UnsetFlagCategory>);
static_assert(FlagInCategory<Verbose, UnsetFlagCategory>);

struct Plain {};
static_assert(!AnyFlag<Plain>);
static_assert(!AnyFlagCategory<Plain>);

// -- Displayable flag --------------------------------------------------------

struct Decorated : Flag<"decorated", NetworkCategory> {
    [[maybe_unused]] static const comms::DisplayInfo& display_info() {
        static const comms::DisplayInfo info{
            .name = "Decorated",
            .icon = comms::Icon::from("mdi:flag"),
        };
        return info;
    }
};

static_assert(comms::Displayable<Decorated>);
static_assert(!comms::Displayable<Ipv6>);

TEST(Flag, FlagRefCapturesDisplayWhenDisplayable) {
    const auto [name, category, display] = FlagRef::of<Decorated>();
    EXPECT_EQ(name, "decorated");
    EXPECT_EQ(category, "network");
    ASSERT_NE(display, nullptr);
    ASSERT_TRUE(display->name.has_value());
    EXPECT_EQ(*display->name, "Decorated");
}

TEST(Flag, FlagRefDisplayNullWhenNotDisplayable) {
    const auto [name, category, display] = FlagRef::of<Ipv6>();
    EXPECT_EQ(name, "ipv6");
    EXPECT_EQ(category, "network");
    EXPECT_EQ(display, nullptr);
}

TEST(Flag, FlagRefEqualityIsByName) {
    const FlagRef a = FlagRef::of<Ipv6>();
    constexpr FlagRef b{.name = "ipv6", .category = "something-else"};
    EXPECT_EQ(a, b);  // identity is the name
    constexpr FlagRef c{.name = "other", .category = "network"};
    EXPECT_NE(a, c);
}

// -- FlagSet -----------------------------------------------------------------

TEST(FlagSet, InsertionOrderPreserved) {
    FlagSet s;
    EXPECT_TRUE(s.insert<Verbose>());
    EXPECT_TRUE(s.insert<Ipv6>());
    EXPECT_TRUE(s.insert<Decorated>());

    std::vector<std::string_view> names;
    for (const auto& f : s) {
        names.push_back(f.name);
    }
    ASSERT_EQ(names.size(), 3U);
    EXPECT_EQ(names[0], "verbose");
    EXPECT_EQ(names[1], "ipv6");
    EXPECT_EQ(names[2], "decorated");
}

TEST(FlagSet, DeduplicatesByName) {
    FlagSet s;
    EXPECT_TRUE(s.insert<Ipv6>());
    EXPECT_FALSE(s.insert<Ipv6>());
    EXPECT_FALSE(s.insert(FlagRef{.name = "ipv6", .category = "other"}));
    EXPECT_EQ(s.size(), 1U);
}

TEST(FlagSet, ContainsCountEraseClear) {
    FlagSet s;
    s.insert<Ipv6>();
    s.insert<Verbose>();

    EXPECT_TRUE(s.contains<Ipv6>());
    EXPECT_TRUE(s.contains("verbose"));
    EXPECT_FALSE(s.contains("missing"));
    EXPECT_EQ(s.count("ipv6"), 1U);
    EXPECT_EQ(s.count("missing"), 0U);

    EXPECT_TRUE(s.erase<Ipv6>());
    EXPECT_FALSE(s.erase<Ipv6>());
    EXPECT_FALSE(s.contains<Ipv6>());
    EXPECT_EQ(s.size(), 1U);

    EXPECT_TRUE(s.erase("verbose"));
    EXPECT_FALSE(s.erase("verbose"));

    EXPECT_TRUE(s.empty());
    s.insert<Ipv6>();
    s.clear();
    EXPECT_TRUE(s.empty());
}

TEST(FlagSet, GroupByCategory) {
    FlagSet s;
    s.insert<Ipv6>();       // network
    s.insert<Verbose>();    // unset
    s.insert<Decorated>();  // network

    const auto groups = s.group_by_category();
    ASSERT_EQ(groups.size(), 2U);
    ASSERT_TRUE(groups.contains("network"));
    ASSERT_TRUE(groups.contains("unset"));

    const auto& network = groups.at("network");
    ASSERT_EQ(network.size(), 2U);
    EXPECT_EQ(network[0].name, "ipv6");  // insertion order within group
    EXPECT_EQ(network[1].name, "decorated");

    const auto& unset = groups.at("unset");
    ASSERT_EQ(unset.size(), 1U);
    EXPECT_EQ(unset[0].name, "verbose");
}

TEST(FlagSet, EqualityIsOrderSensitive) {
    FlagSet a;
    a.insert<Ipv6>();
    a.insert<Verbose>();

    FlagSet b;
    b.insert<Ipv6>();
    b.insert<Verbose>();
    EXPECT_EQ(a, b);

    FlagSet c;
    c.insert<Verbose>();
    c.insert<Ipv6>();
    EXPECT_NE(a, c);  // same flags, different order
}

// -- mixins: HasFlags / FlagBuilderMixin -------------------------------------

using comms::AllowedFlag;

// Constraint: an empty list accepts anything; a category list restricts.
static_assert(AllowedFlag<Verbose>);                // unconstrained
static_assert(AllowedFlag<Ipv6, NetworkCategory>);  // in category
static_assert(AllowedFlag<Decorated, NetworkCategory>);
static_assert(!AllowedFlag<Verbose, NetworkCategory>);  // wrong category

// A constrained builder: typed overloads only accept NetworkCategory flags.
// FlagBuilderGetters gives it the fluent mutators plus a public IHasFlags read API.
class NetworkConfig : public comms::FlagBuilderGetters<NetworkConfig, NetworkCategory> {};

static_assert(NetworkConfig::flag_allowed<Ipv6>);
static_assert(!NetworkConfig::flag_allowed<Verbose>);
static_assert(std::derived_from<NetworkConfig, comms::IHasFlags>);

// A silent builder: bare FlagBuilderMixin keeps read access protected (it is not
// an IHasFlags), using it only internally.
class SilentBuilder : public comms::FlagBuilderMixin<SilentBuilder> {
public:
    [[nodiscard]] bool enabled(const std::string_view name) const {
        return flags().contains(name);  // protected member, reachable from the derived type
    }
};

static_assert(!std::derived_from<SilentBuilder, comms::IHasFlags>);

// A plain holder that populates its protected FlagSet directly.
class AnyHolder : public comms::HasFlags<> {
public:
    void seed() {
        flags_.insert<Verbose>();
        flags_.insert<Ipv6>();
    }
};

static_assert(std::derived_from<AnyHolder, comms::IHasFlags>);

TEST(HasFlags, HolderExposesFlags) {
    AnyHolder h;
    EXPECT_TRUE(h.flags().empty());
    h.seed();
    EXPECT_EQ(h.flags().size(), 2U);
    EXPECT_TRUE(h.has_flag<Verbose>());
    EXPECT_TRUE(h.has_flag("ipv6"));
    EXPECT_FALSE(h.has_flag("missing"));
}

TEST(FlagBuilderMixin, FluentInsertReturnsDerivedAndKeepsOrder) {
    NetworkConfig cfg;
    NetworkConfig& ref = cfg.insert_flag<Ipv6>().insert_flag<Decorated>();
    EXPECT_EQ(&ref, &cfg);  // chaining yields the derived type

    ASSERT_EQ(cfg.flags().size(), 2U);
    auto it = cfg.flags().begin();
    EXPECT_EQ(it->name, "ipv6");
    ++it;
    EXPECT_EQ(it->name, "decorated");
}

TEST(FlagBuilderMixin, SetFlagTogglesPresence) {
    NetworkConfig cfg;
    cfg.set_flag<Ipv6>(true);
    EXPECT_TRUE(cfg.has_flag<Ipv6>());
    cfg.set_flag<Ipv6>(false);
    EXPECT_FALSE(cfg.has_flag<Ipv6>());
}

TEST(FlagBuilderMixin, RemoveAndClear) {
    NetworkConfig cfg;
    cfg.insert_flag<Ipv6>().insert_flag<Decorated>();
    cfg.remove_flag<Ipv6>();
    EXPECT_FALSE(cfg.has_flag<Ipv6>());
    EXPECT_TRUE(cfg.has_flag("decorated"));

    cfg.remove_flag("decorated");
    EXPECT_TRUE(cfg.flags().empty());

    cfg.insert_flag<Decorated>().clear_flags();
    EXPECT_TRUE(cfg.flags().empty());
}

TEST(FlagBuilderMixin, SetFlagsReplacesWholeSet) {
    FlagSet src;
    src.insert<Decorated>();

    NetworkConfig cfg;
    cfg.insert_flag<Ipv6>().set_flags(src);
    ASSERT_EQ(cfg.flags().size(), 1U);
    EXPECT_TRUE(cfg.has_flag<Decorated>());
    EXPECT_FALSE(cfg.has_flag<Ipv6>());
}

TEST(FlagBuilderMixin, RuntimeInsertOverloadIsUnconstrained) {
    // The FlagRef overload bypasses the category constraint by design.
    NetworkConfig cfg;
    cfg.insert_flag(FlagRef::of<Verbose>());
    EXPECT_TRUE(cfg.has_flag("verbose"));
}

TEST(FlagBuilderMixin, SilentBuilderKeepsFlagsInternal) {
    // SilentBuilder is not an IHasFlags; flags() stays protected, used internally.
    SilentBuilder b;
    b.flag<Verbose>().flag<Ipv6>();
    EXPECT_TRUE(b.enabled("verbose"));
    EXPECT_TRUE(b.enabled("ipv6"));
    EXPECT_FALSE(b.enabled("missing"));
    // b.flags() is not part of SilentBuilder's public API.
}

TEST(IHasFlags, PolymorphicReadAcrossHolderAndBuilder) {
    AnyHolder holder;
    holder.seed();  // verbose, ipv6

    NetworkConfig builder;
    builder.flag<Ipv6>();

    // Both a HasFlags holder and a FlagBuilderGetters builder read through the
    // same interface reference.
    const comms::IHasFlags& a = holder;
    const comms::IHasFlags& b = builder;

    EXPECT_EQ(a.flags().size(), 2U);
    EXPECT_TRUE(a.has_flag<Verbose>());
    EXPECT_TRUE(a.has_flag("ipv6"));

    EXPECT_EQ(b.flags().size(), 1U);
    EXPECT_TRUE(b.has_flag("ipv6"));
    EXPECT_FALSE(b.has_flag("verbose"));
}

}  // namespace

// -- macros ------------------------------------------------------------------
// Define + auto-register flags at namespace scope (file scope), then assert
// they landed in the GlobalFlagRegistry.

COMMONS_DEFINE_FLAG(GlobalAlpha, "global.alpha");
COMMONS_FLAG_CATEGORY(FeatureCat, "feature");
COMMONS_DEFINE_FLAG_IN(GlobalBeta, "global.beta", FeatureCat);

// A separately-defined type registered manually.
COMMONS_FLAG_IN(GlobalGamma, "global.gamma", FeatureCat);
COMMONS_REGISTER_FLAG(GlobalGamma);

// A whole family in one line: category + per-category template + concept.
COMMONS_FLAG_FAMILY(MyLibCat, "mylib", MyLibFlag, IsMyLibFlag);

namespace {

TEST(FlagMacros, DefineFlagRegistersInGlobalRegistry) {
    const auto& reg = GlobalFlagRegistry::instance();
    EXPECT_TRUE(reg.flags().contains("global.alpha"));

    const auto found = reg.find("global.alpha");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "global.alpha");
    EXPECT_EQ(found->category, "unset");
}

TEST(FlagMacros, DefineFlagInRegistersWithCategory) {
    const auto found = GlobalFlagRegistry::instance().find("global.beta");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->category, "feature");
}

TEST(FlagMacros, RegisterFlagRegistersSeparatelyDefinedType) {
    const auto found = GlobalFlagRegistry::instance().find("global.gamma");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->category, "feature");
}

TEST(FlagMacros, FlagFamilyProducesWorkingCategoryTemplateConcept) {
    static_assert(MyLibCat::name == "mylib");

    using FastFlag = MyLibFlag<"fast">;
    static_assert(FastFlag::name == "fast");
    static_assert(FastFlag::category_name == "mylib");
    static_assert(std::same_as<FastFlag::category, MyLibCat>);

    static_assert(IsMyLibFlag<FastFlag>);
    static_assert(!IsMyLibFlag<GlobalAlpha>);

    FlagSet s;
    s.insert<FastFlag>();
    EXPECT_TRUE(s.contains("fast"));
}

}  // namespace
