#include <commons/reason.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <concepts>
#include <format>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using comms::CancelException;
using comms::Exception;
using comms::FailureReasonException;
using comms::FailureReasonPtr;
using comms::GenericFailureReason;
using comms::GenericReason;
using comms::GlobalReasonRegistry;
using comms::IFailureReason;
using comms::IReason;
using comms::ReasonException;
using comms::ReasonPtr;
using comms::RejectException;
using comms::UnknownFailureReason;
using comms::UnknownReason;

// A custom failure reason defined + registered via the one-line macro,
// exercising the open set, the IFailureReason branch, and the inherited
// ReasonKind constructors. It defines no display_info(), so info() is empty.
COMMONS_DEFINE_FAILURE_REASON(RateLimitReason, "rate_limit");

// A custom plain reason via the macro too.
COMMONS_DEFINE_REASON(NotFoundReason, "not_found");

static_assert(std::derived_from<RateLimitReason, IFailureReason>);
static_assert(!comms::Displayable<RateLimitReason>);  // display_info() is optional

// A user-defined sub-interface — the way IFailureReason refines IReason — used to
// exercise the registry's generic kinds_of<Base>() filter on a custom subkind.
class IRetryableReason : public IReason {
protected:
    IRetryableReason() = default;
    IRetryableReason(const IRetryableReason&) = default;
    IRetryableReason(IRetryableReason&&) = default;
    IRetryableReason& operator=(const IRetryableReason&) = default;
    IRetryableReason& operator=(IRetryableReason&&) = default;
};

class RetryReason final : public comms::ReasonKind<"retry", RetryReason, IRetryableReason> {
public:
    using comms::ReasonKind<"retry", RetryReason, IRetryableReason>::ReasonKind;
};
COMMONS_REGISTER_REASON(RetryReason);

// -- kind / fields -----------------------------------------------------------

TEST(Reason, KindPerType) {
    EXPECT_EQ(GenericReason{}.kind(), "generic");
    EXPECT_EQ(UnknownReason{}.kind(), "unknown");
    EXPECT_EQ(GenericFailureReason{}.kind(), "generic_failure");
    EXPECT_EQ(UnknownFailureReason{}.kind(), "unknown_failure");
    EXPECT_EQ(GenericReason::KIND, "generic");
}

TEST(Reason, DefaultMessages) {
    EXPECT_EQ(GenericReason{}.message, "Generic reason");
    EXPECT_EQ(UnknownReason{}.message, "Unknown reason");
    EXPECT_EQ(GenericFailureReason{}.message, "Generic failure reason");
    EXPECT_EQ(UnknownFailureReason{}.message, "Unknown failure reason");
    EXPECT_EQ(GenericReason{}.code, 0);
}

TEST(Reason, ConstructorOverloads) {
    EXPECT_EQ(GenericReason{"boom"}.message, "boom");
    EXPECT_EQ(GenericReason{42}.code, 42);
    EXPECT_EQ(GenericReason{42}.message, "Generic reason");

    const GenericReason both{7, "seven"};
    EXPECT_EQ(both.code, 7);
    EXPECT_EQ(both.message, "seven");
}

TEST(Reason, CreatedAtIsStampedAtConstruction) {
    const auto before = comms::ReasonClock::now();
    const GenericReason r;
    const auto after = comms::ReasonClock::now();
    EXPECT_GE(r.created_at, before);
    EXPECT_LE(r.created_at, after);
}

// -- clone -------------------------------------------------------------------

TEST(Reason, CloneIsAnIndependentDeepCopy) {
    GenericReason original{1, "first"};
    const ReasonPtr copy = original.clone();
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->kind(), "generic");
    EXPECT_EQ(copy->code, 1);
    EXPECT_EQ(copy->message, "first");

    original.message = "changed";       // mutate the original
    EXPECT_EQ(copy->message, "first");  // the clone is unaffected
}

TEST(Reason, CloneThroughBasePointer) {
    const ReasonPtr r = std::make_unique<UnknownReason>();
    const ReasonPtr c = r->clone();
    EXPECT_EQ(c->kind(), "unknown");
}

// -- info() / Displayable ----------------------------------------------------

static_assert(comms::Displayable<GenericReason>);
static_assert(comms::Displayable<GenericFailureReason>);

TEST(Reason, InfoReturnsDisplayInfo) {
    const GenericReason r;
    EXPECT_EQ(r.info().name, "Generic");
    EXPECT_EQ(&r.info(), &comms::display_info<GenericReason>());
}

// -- factories ---------------------------------------------------------------

TEST(Reason, FactoryHelpers) {
    const ReasonPtr generic = comms::make_reason("oops");
    EXPECT_EQ(generic->kind(), "generic");
    EXPECT_EQ(generic->message, "oops");

    const ReasonPtr unknown = comms::make_reason<UnknownReason>(404);
    EXPECT_EQ(unknown->kind(), "unknown");
    EXPECT_EQ(unknown->code, 404);

    const FailureReasonPtr failure = comms::make_failure_reason(13, "bad");
    EXPECT_EQ(failure->kind(), "generic_failure");
    EXPECT_EQ(failure->code, 13);
}

// -- ordering ----------------------------------------------------------------

TEST(Reason, OrderingByCreatedAt) {
    const GenericReason earlier;
    GenericReason later;
    later.created_at = earlier.created_at + std::chrono::seconds{1};
    EXPECT_TRUE(earlier < later);
    EXPECT_FALSE(later < earlier);
    EXPECT_TRUE((earlier <=> later) == std::strong_ordering::less);
}

// -- registry ----------------------------------------------------------------

TEST(Reason, RegistryContainsBuiltins) {
    const auto& reg = GlobalReasonRegistry::instance();
    EXPECT_TRUE(reg.contains("generic"));
    EXPECT_TRUE(reg.contains("unknown"));
    EXPECT_TRUE(reg.contains("generic_failure"));
    EXPECT_TRUE(reg.contains("unknown_failure"));
    EXPECT_FALSE(reg.contains("nope"));
}

TEST(Reason, RegistryCreatesByKind) {
    const ReasonPtr r = GlobalReasonRegistry::instance().create("unknown_failure");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->kind(), "unknown_failure");
}

TEST(Reason, RegistryReturnsNullForUnknownKind) {
    EXPECT_EQ(GlobalReasonRegistry::instance().create("nope"), nullptr);
}

TEST(Reason, MacroDefinesAndRegistersCustomKinds) {
    const auto& reg = GlobalReasonRegistry::instance();
    EXPECT_TRUE(reg.contains("rate_limit"));
    EXPECT_TRUE(reg.contains("not_found"));

    const ReasonPtr r = reg.create("rate_limit");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->kind(), "rate_limit");
    EXPECT_FALSE(r->info().name.has_value());  // no display_info() defined

    // The inherited ReasonKind constructors work through the factory helper.
    const FailureReasonPtr fr = comms::make_failure_reason<RateLimitReason>(429, "slow down");
    EXPECT_EQ(fr->code, 429);
    EXPECT_EQ(fr->message, "slow down");
}

// -- registry filters --------------------------------------------------------

namespace {
[[nodiscard]] bool has(const std::vector<std::string>& v, const std::string_view name) {
    return std::ranges::find(v, name) != v.end();
}
}  // namespace

TEST(Reason, RegistryFiltersByFamily) {
    const auto& reg = GlobalReasonRegistry::instance();

    // All kinds.
    const auto all = reg.kinds();
    EXPECT_TRUE(has(all, "generic"));
    EXPECT_TRUE(has(all, "generic_failure"));
    EXPECT_TRUE(has(all, "rate_limit"));
    EXPECT_TRUE(has(all, "retry"));

    // Failure reasons only.
    const auto failures = reg.failure_kinds();
    EXPECT_TRUE(has(failures, "generic_failure"));
    EXPECT_TRUE(has(failures, "unknown_failure"));
    EXPECT_TRUE(has(failures, "rate_limit"));  // a custom failure kind
    EXPECT_FALSE(has(failures, "generic"));    // a plain reason
    EXPECT_FALSE(has(failures, "retry"));

    // Non-failure reasons only — the complement.
    const auto non_failures = reg.non_failure_kinds();
    EXPECT_TRUE(has(non_failures, "generic"));
    EXPECT_TRUE(has(non_failures, "unknown"));
    EXPECT_TRUE(has(non_failures, "not_found"));
    EXPECT_TRUE(has(non_failures, "retry"));
    EXPECT_FALSE(has(non_failures, "generic_failure"));

    // A custom sub-interface family via the generic filter.
    const auto retryable = reg.kinds_of<IRetryableReason>();
    EXPECT_TRUE(has(retryable, "retry"));
    EXPECT_FALSE(has(retryable, "generic"));
    EXPECT_FALSE(has(retryable, "rate_limit"));

    // kinds_of<IReason>() is every kind.
    EXPECT_EQ(reg.kinds_of<IReason>().size(), all.size());
}

// -- exceptions --------------------------------------------------------------

TEST(Reason, ReasonExceptionCarriesReasonAndMessage) {
    try {
        throw ReasonException(GenericReason{5, "carried"});
    } catch (const ReasonException& e) {
        EXPECT_STREQ(e.what(), "carried");
        ASSERT_NE(e.reason(), nullptr);
        EXPECT_EQ(e.reason()->code, 5);
    }
}

TEST(Reason, ThrowAsExceptionThrowsFailureReasonException) {
    const GenericFailureReason fr{99, "developer failure"};
    try {
        fr.throw_as_exception();
        FAIL() << "expected throw";
    } catch (const FailureReasonException& e) {
        EXPECT_STREQ(e.what(), "developer failure");
        ASSERT_NE(e.failure_reason(), nullptr);
        EXPECT_EQ(e.failure_reason()->code, 99);
        EXPECT_EQ(e.failure_reason()->kind(), "generic_failure");
        // The typed failure_reason() and the base reason() are the same object.
        EXPECT_EQ(e.failure_reason().get(), e.reason().get());
    }
}

TEST(Reason, ExceptionHierarchyIsCatchableAtEachLevel) {
    // FailureReasonException -> ReasonException -> comms::Exception -> std::exception
    EXPECT_THROW(throw FailureReasonException(GenericFailureReason{}), ReasonException);
    EXPECT_THROW(throw FailureReasonException(GenericFailureReason{}), Exception);
    EXPECT_THROW(throw FailureReasonException(GenericFailureReason{}), std::exception);

    EXPECT_THROW(throw RejectException(GenericReason{"rejected"}), ReasonException);
    EXPECT_THROW(throw CancelException(GenericReason{"cancelled"}), Exception);
}

TEST(Reason, RejectAndCancelCarryReason) {
    try {
        throw RejectException(GenericReason{1, "no"});
    } catch (const ReasonException& e) {
        ASSERT_NE(e.reason(), nullptr);
        EXPECT_EQ(e.reason()->message, "no");
    }
}

// -- title / text output -----------------------------------------------------

TEST(Reason, TitleIsTheConcreteTypeName) {
    // The generic/unknown built-ins carry their code in the title. (Extra parens
    // guard the brace-init commas from the EXPECT_EQ macro.)
    EXPECT_EQ((GenericReason{7, "hi"}.title()), "GenericReason(7)");
    EXPECT_EQ((UnknownReason{3, "x"}.title()), "UnknownReason(3)");
    EXPECT_EQ(GenericFailureReason{9}.title(), "GenericFailureReason(9)");

    // A custom kind shows just its type name (namespace qualification stripped),
    // since it doesn't override title().
    EXPECT_EQ(RateLimitReason{}.title(), "RateLimitReason");

    // Through a base reference the dynamic type still wins.
    const ReasonPtr r = std::make_unique<GenericReason>(5, "z");
    EXPECT_EQ(r->title(), "GenericReason(5)");
}

TEST(Reason, ToStringFormat) {
    EXPECT_EQ(comms::to_string(GenericReason{7, "hi"}), "GenericReason(7): hi");
    // A custom kind: type name, no code.
    EXPECT_EQ(comms::to_string(RateLimitReason{0, "slow down"}), "RateLimitReason: slow down");
}

TEST(Reason, OstreamInsertion) {
    std::ostringstream os;
    os << UnknownReason{3, "x"};
    EXPECT_EQ(os.str(), "UnknownReason(3): x");
}

TEST(Reason, StdFormat) {
    EXPECT_EQ(std::format("{}", GenericReason{1, "f"}), "GenericReason(1): f");
    const ReasonPtr r = std::make_unique<UnknownReason>(2, "g");
    EXPECT_EQ(std::format("{}", *r), "UnknownReason(2): g");  // through an IReason reference
}

}  // namespace
