#include <commons/origin.hpp>

#include <gtest/gtest.h>

#include <format>
#include <memory>
#include <sstream>
#include <string>

namespace {

using comms::CoreOrigin;
using comms::ExternalOrigin;
using comms::GlobalOriginRegistry;
using comms::InternalOrigin;
using comms::IOrigin;
using comms::OriginPtr;
using comms::UnknownOrigin;

// A custom origin defined + registered via the macro, exercising the open set.
class TestOrigin final : public comms::OriginKind<"test", TestOrigin> {
public:
    [[nodiscard]] [[maybe_unused]] static const comms::DisplayInfo& display_info() {
        static const comms::DisplayInfo info{.name = "Test", .description = "A test origin."};
        return info;
    }
};
COMMONS_REGISTER_ORIGIN(TestOrigin);

// -- kind --------------------------------------------------------------------

TEST(Origin, KindPerType) {
    EXPECT_EQ(CoreOrigin{}.kind(), "core");
    EXPECT_EQ(InternalOrigin{}.kind(), "internal");
    EXPECT_EQ(ExternalOrigin{}.kind(), "external");
    EXPECT_EQ(UnknownOrigin{}.kind(), "unknown");
    EXPECT_EQ(CoreOrigin::KIND, "core");
}

// -- clone -------------------------------------------------------------------

TEST(Origin, CloneIsAnIndependentDeepCopy) {
    ExternalOrigin original{"npm"};
    const OriginPtr copy = original.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->kind(), "external");

    auto* ext = dynamic_cast<ExternalOrigin*>(copy.get());
    ASSERT_NE(ext, nullptr);
    EXPECT_EQ(ext->source, "npm");

    original.source = "changed";    // mutate the original
    EXPECT_EQ(ext->source, "npm");  // the clone is unaffected
}

TEST(Origin, CloneThroughBasePointer) {
    const OriginPtr o = std::make_unique<CoreOrigin>();
    const OriginPtr c = o->clone();
    EXPECT_EQ(c->kind(), "core");
}

// -- info() / Displayable ----------------------------------------------------

static_assert(comms::Displayable<CoreOrigin>);
static_assert(comms::Displayable<ExternalOrigin>);

TEST(Origin, InfoReturnsDisplayInfo) {
    const CoreOrigin core;
    EXPECT_EQ(core.info().name, "Core");
    EXPECT_EQ(core.info().description, "Registered by the host core itself.");
    // The polymorphic info() and the static display_info() are the same object.
    EXPECT_EQ(&core.info(), &comms::display_info<CoreOrigin>());
}

TEST(Origin, InfoThroughBasePointer) {
    const OriginPtr o = std::make_unique<ExternalOrigin>("crates.io");
    EXPECT_EQ(o->info().name, "External");
}

// -- registry ----------------------------------------------------------------

TEST(Origin, RegistryContainsBuiltins) {
    const auto& reg = GlobalOriginRegistry::instance();
    EXPECT_TRUE(reg.contains("core"));
    EXPECT_TRUE(reg.contains("internal"));
    EXPECT_TRUE(reg.contains("external"));
    EXPECT_TRUE(reg.contains("unknown"));
    EXPECT_FALSE(reg.contains("nope"));
}

TEST(Origin, RegistryCreatesByKind) {
    const auto& reg = GlobalOriginRegistry::instance();
    const OriginPtr o = reg.create("external");
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(o->kind(), "external");
}

TEST(Origin, RegistryReturnsNullForUnknownKind) {
    EXPECT_EQ(GlobalOriginRegistry::instance().create("nope"), nullptr);
}

TEST(Origin, MacroRegistersCustomKind) {
    const auto& reg = GlobalOriginRegistry::instance();
    EXPECT_TRUE(reg.contains("test"));
    const OriginPtr o = reg.create("test");
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(o->kind(), "test");
    EXPECT_EQ(o->info().name, "Test");
}

// -- text output -------------------------------------------------------------

TEST(Origin, ToStringIsKind) {
    EXPECT_EQ(comms::to_string(CoreOrigin{}), "core");
    EXPECT_EQ(comms::to_string(ExternalOrigin{"x"}), "external");
}

TEST(Origin, OstreamInsertion) {
    std::ostringstream os;
    os << InternalOrigin{};
    EXPECT_EQ(os.str(), "internal");
}

TEST(Origin, StdFormat) {
    EXPECT_EQ(std::format("{}", UnknownOrigin{}), "unknown");
    const OriginPtr o = std::make_unique<CoreOrigin>();
    EXPECT_EQ(std::format("{}", *o), "core");  // through an IOrigin reference
}

}  // namespace
