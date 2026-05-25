#include <commons/prioritized.hpp>

#include <gtest/gtest.h>

#include <concepts>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using comms::get_priority;
using comms::LenientPrioritizedCompare;
using comms::make_prioritized;
using comms::Prioritizable;
using comms::Prioritized;
using comms::PrioritizedBuilder;
using comms::PrioritizedCompare;
using comms::PrioritizedSet;
using comms::with_priority;
using comms::with_priority_inherits;
using comms::WithPriority;

// -- sample types ------------------------------------------------------------

// A bare derived type: no override, so it reports DEFAULT_PRIORITY.
struct BareAdapter : Prioritized {};

// A derived type overriding priority().
struct DerivedPri : Prioritized {
    int p;
    explicit DerivedPri(const int v) noexcept : p(v) {}
    [[nodiscard]] int priority() const noexcept override {
        return p;
    }
};

// A standalone Prioritizable type (does NOT derive from Prioritized). Identity
// is by `id`; `prio` is its self-reported priority.
struct Widget {
    int id;
    int prio;
    [[maybe_unused]] [[nodiscard]] int priority() const noexcept {
        return prio;
    }
    bool operator==(const Widget& o) const noexcept {
        return id == o.id;
    }
};

struct Plain {};  // neither polymorphic nor Prioritizable

struct Poly {  // polymorphic but not Prioritizable, not derived from Prioritized
    virtual ~Poly() = default;
};

// Polymorphic, reaches Prioritized only via a cross-cast from Poly*.
struct Mixed : Poly, Prioritized {
    [[nodiscard]] int priority() const noexcept override {
        return 4;
    }
};

static_assert(Prioritizable<DerivedPri>);
static_assert(Prioritizable<Widget>);
static_assert(Prioritizable<BareAdapter>);
static_assert(!Prioritizable<Plain>);

// -- Prioritized interface ---------------------------------------------------

TEST(Prioritized, SentinelOrdering) {
    EXPECT_LT(Prioritized::HIGHEST_PRECEDENCE, Prioritized::DEFAULT_PRIORITY);
    EXPECT_LT(Prioritized::DEFAULT_PRIORITY, Prioritized::LOWEST_PRECEDENCE);
    EXPECT_EQ(Prioritized::DEFAULT_PRIORITY, COMMONS_PRIORITIZED_DEFAULT_PRIORITY);
}

TEST(Prioritized, BareDerivedReportsDefault) {
    const BareAdapter a;
    EXPECT_EQ(a.priority(), Prioritized::DEFAULT_PRIORITY);
}

TEST(Prioritized, OverrideReportsOwnValue) {
    const DerivedPri a{7};
    EXPECT_EQ(a.priority(), 7);
    const Prioritized& base = a;
    EXPECT_EQ(base.priority(), 7);
}

// -- get_priority dispatch ---------------------------------------------------

TEST(GetPriority, ValueAndReference) {
    const DerivedPri d{5};
    EXPECT_EQ(get_priority(d), 5);  // base-of branch
    constexpr Widget w{.id = 1, .prio = 8};
    EXPECT_EQ(get_priority(w), 8);  // Prioritizable branch
    EXPECT_EQ(get_priority(Plain{}), Prioritized::DEFAULT_PRIORITY);
}

TEST(GetPriority, RawPointer) {
    const DerivedPri d{5};
    EXPECT_EQ(get_priority(&d), 5);
    constexpr Widget w{.id = 1, .prio = 8};
    EXPECT_EQ(get_priority(&w), 8);
    const DerivedPri* null = nullptr;
    EXPECT_EQ(get_priority(null), Prioritized::DEFAULT_PRIORITY);
}

TEST(GetPriority, SmartPointer) {
    const auto sp = std::make_shared<DerivedPri>(11);
    EXPECT_EQ(get_priority(sp), 11);  // regression guard for bug #1
    const std::shared_ptr<DerivedPri> empty;
    EXPECT_EQ(get_priority(empty), Prioritized::DEFAULT_PRIORITY);

    // shared_ptr to a standalone Prioritizable also resolves.
    const auto wp = std::make_shared<Widget>(Widget{.id = 1, .prio = 9});
    EXPECT_EQ(get_priority(wp), 9);
}

TEST(GetPriority, NonPolymorphicFallsBack) {
    constexpr Plain p;
    EXPECT_EQ(get_priority(&p), Prioritized::DEFAULT_PRIORITY);
}

TEST(GetPriority, PolymorphicProbedViaDynamicCast) {
    const Mixed m;
    const Poly* as_poly = &m;
    EXPECT_EQ(get_priority(as_poly), 4);  // cross-cast Poly* -> Prioritized*

    const Poly plain_poly;
    EXPECT_EQ(get_priority(&plain_poly), Prioritized::DEFAULT_PRIORITY);  // cast fails
}

// -- comparators -------------------------------------------------------------

TEST(PrioritizedCompare, OrdersByPriorityWithStableTieBreak) {
    constexpr PrioritizedCompare cmp;
    const std::shared_ptr<Prioritized> a = std::make_shared<DerivedPri>(5);
    const std::shared_ptr<Prioritized> b = std::make_shared<DerivedPri>(1);
    EXPECT_TRUE(cmp(b, a));  // 1 < 5
    EXPECT_FALSE(cmp(a, b));
    EXPECT_FALSE(cmp(a, a));  // equal pointer -> false (irreflexive)

    const std::shared_ptr<Prioritized> c = std::make_shared<DerivedPri>(5);
    EXPECT_NE(cmp(a, c), cmp(c, a));  // tie broken consistently one way

    const std::shared_ptr<Prioritized> null;
    EXPECT_TRUE(cmp(null, a));  // DEFAULT (0) < 5
}

TEST(LenientPrioritizedCompare, WorksOverNonDerivedType) {
    constexpr LenientPrioritizedCompare<Widget> cmp;
    const auto a = std::make_shared<Widget>(Widget{.id = 1, .prio = 5});
    const auto b = std::make_shared<Widget>(Widget{.id = 2, .prio = 2});
    EXPECT_TRUE(cmp(b, a));
    EXPECT_FALSE(cmp(a, b));
    EXPECT_FALSE(cmp(a, a));

    // Usable as a std::set comparator.
    std::set<std::shared_ptr<Widget>, LenientPrioritizedCompare<Widget>> set;
    set.insert(a);
    set.insert(b);
    EXPECT_EQ((*set.begin())->prio, 2);
}

// -- PrioritizedSet: transparency --------------------------------------------

static_assert(std::is_same_v<PrioritizedSet<std::string>::value_type, std::string>);
static_assert(std::is_same_v<PrioritizedSet<std::string>::key_type, std::string>);
static_assert(std::is_same_v<decltype(*std::declval<PrioritizedSet<std::string>>().begin()),
                             const std::string&>);

TEST(PrioritizedSet, IteratorsYieldT) {
    PrioritizedSet<std::string> s;
    s.insert("a");
    const std::string& ref = *s.begin();  // deref is const T&
    EXPECT_EQ(ref, "a");
    EXPECT_EQ(s.begin()->size(), 1U);  // operator-> projects to T
}

TEST(PrioritizedSet, UniqueByValue) {
    PrioritizedSet<int> s;
    auto [it1, ok1] = s.insert(42);
    EXPECT_TRUE(ok1);
    auto [it2, ok2] = s.insert(42);
    EXPECT_FALSE(ok2);  // duplicate value
    EXPECT_EQ(it1, it2);
    EXPECT_EQ(s.size(), 1U);
}

TEST(PrioritizedSet, FindEraseContainsCount) {
    PrioritizedSet<int> s{1, 2, 3};
    EXPECT_TRUE(s.contains(2));
    EXPECT_EQ(s.count(2), 1U);
    EXPECT_EQ(s.count(99), 0U);
    EXPECT_NE(s.find(2), s.end());
    EXPECT_EQ(s.find(99), s.end());

    EXPECT_EQ(s.erase(2), 1U);
    EXPECT_EQ(s.erase(2), 0U);
    EXPECT_FALSE(s.contains(2));
    EXPECT_EQ(s.size(), 2U);

    auto it = s.find(1);
    auto next = s.erase(it);  // erase by iterator
    EXPECT_EQ(*next, 3);
    EXPECT_EQ(s.size(), 1U);
}

TEST(PrioritizedSet, OrdersByPriorityThenInsertionOrder) {
    PrioritizedSet<std::string> s;
    s.insert(5, "a");  // order 0
    s.insert(1, "b");  // order 1
    s.insert(5, "c");  // order 2
    s.insert(1, "d");  // order 3

    std::vector<std::string> seen;
    for (const auto& v : s) {
        seen.push_back(v);
    }
    // (priority asc, insertion asc): b(1), d(1), a(5), c(5)
    ASSERT_EQ(seen.size(), 4U);
    EXPECT_EQ(seen[0], "b");
    EXPECT_EQ(seen[1], "d");
    EXPECT_EQ(seen[2], "a");
    EXPECT_EQ(seen[3], "c");
}

TEST(PrioritizedSet, PriorityIsSnapshotAtInsert) {
    PrioritizedSet<Widget> s;
    s.insert(Widget{.id = 1, .prio = 10});  // snapshot = 10 (from get_priority)
    EXPECT_EQ(s.priority_of(Widget{.id = 1, .prio = 0}), 10);

    // Re-inserting an equal-by-id widget is a no-op; snapshot stays 10.
    auto [it, ok] = s.insert(Widget{.id = 1, .prio = 99});
    EXPECT_FALSE(ok);
    EXPECT_EQ(s.priority_of(Widget{.id = 1, .prio = 0}), 10);
}

TEST(PrioritizedSet, SetPriorityReorders) {
    PrioritizedSet<std::string> s;
    s.insert(1, "a");
    s.insert(2, "b");
    s.insert(3, "c");
    EXPECT_EQ(*s.begin(), "a");

    EXPECT_TRUE(s.set_priority("c", 0));  // c now sorts first
    EXPECT_EQ(*s.begin(), "c");
    EXPECT_EQ(s.priority_of("c"), 0);

    EXPECT_FALSE(s.set_priority("missing", 0));
}

TEST(PrioritizedSet, ReverseIteration) {
    PrioritizedSet<int> s;
    s.insert(1, 100);
    s.insert(2, 200);
    s.insert(3, 300);

    std::vector<int> rev;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        rev.push_back(*it);
    }
    ASSERT_EQ(rev.size(), 3U);
    EXPECT_EQ(rev[0], 300);
    EXPECT_EQ(rev[1], 200);
    EXPECT_EQ(rev[2], 100);
}

TEST(PrioritizedSet, InitializerListAndRangeConstruction) {
    const PrioritizedSet<int> il{3, 1, 2, 1};  // dedups; all DEFAULT priority
    EXPECT_EQ(il.size(), 3U);
    // Equal priority -> insertion order preserved.
    const std::vector<int> seen(il.begin(), il.end());
    EXPECT_EQ(seen, (std::vector<int>{3, 1, 2}));

    const std::vector<int> src{10, 20, 30};
    const PrioritizedSet<int> rng(src.begin(), src.end());
    EXPECT_EQ(rng.size(), 3U);
    EXPECT_TRUE(rng.contains(20));
}

TEST(PrioritizedSet, EqualityClearSwapEmpty) {
    PrioritizedSet<int> a{1, 2, 3};
    const PrioritizedSet<int> b{1, 2, 3};
    EXPECT_EQ(a, b);

    PrioritizedSet<int> c;
    c.insert(0, 2);  // different priority -> different order
    c.insert(0, 1);
    c.insert(0, 3);
    EXPECT_NE(a, c);  // sequence is 2,1,3 vs 1,2,3

    a.swap(c);
    EXPECT_EQ(*a.begin(), 2);
    a.clear();
    EXPECT_TRUE(a.empty());
}

TEST(PrioritizedSet, EmplaceConstructsAndInserts) {
    PrioritizedSet<std::string> s;
    auto [it, ok] = s.emplace("xy");  // std::string("xy")
    EXPECT_TRUE(ok);
    EXPECT_EQ(*it, "xy");
}

// -- PrioritizedBuilder ------------------------------------------------------

struct MyAdapter : PrioritizedBuilder<MyAdapter> {
    [[maybe_unused]] int payload = 0;
};

static_assert(std::derived_from<MyAdapter, Prioritized>);

TEST(PrioritizedBuilder, GetterSetterOverloadAndChaining) {
    MyAdapter a;
    EXPECT_EQ(a.priority(), Prioritized::DEFAULT_PRIORITY);  // getter

    MyAdapter& ref = a.priority(5);  // setter returns Derived&
    EXPECT_EQ(&ref, &a);
    EXPECT_EQ(a.priority(), 5);

    a.highest_priority();
    EXPECT_EQ(a.priority(), Prioritized::HIGHEST_PRECEDENCE);
    a.lowest_priority();
    EXPECT_EQ(a.priority(), Prioritized::LOWEST_PRECEDENCE);

    // Fluent chain.
    a.priority(3).highest_priority();
    EXPECT_EQ(a.priority(), Prioritized::HIGHEST_PRECEDENCE);

    // Readable polymorphically and via get_priority.
    a.priority(42);
    const Prioritized& base = a;
    EXPECT_EQ(base.priority(), 42);
    EXPECT_EQ(get_priority(a), 42);
}

// -- WithPriority ------------------------------------------------------------

struct Engine {  // non-final class -> inheritance flavor
    int rpm = 0;
    explicit Engine(const int r = 0) noexcept : rpm(r) {}
    [[nodiscard]] int rev() const noexcept {
        return rpm;
    }
};

struct FinalThing final {
    [[maybe_unused]] int v = 0;
};

static_assert(with_priority_inherits<Engine>);
static_assert(!with_priority_inherits<int>);
static_assert(!with_priority_inherits<FinalThing>);
static_assert(std::is_base_of_v<Engine, WithPriority<Engine>>);
static_assert(std::is_base_of_v<Prioritized, WithPriority<Engine>>);
static_assert(std::is_base_of_v<Prioritized, WithPriority<int>>);

int rev_of(const Engine& e) {
    return e.rev();
}

TEST(WithPriority, InheritanceFlavorIsAUsableT) {
    auto w = with_priority(7, Engine{1200});
    EXPECT_EQ(w.priority(), 7);
    EXPECT_EQ(w.value().rpm, 1200);
    EXPECT_EQ(rev_of(w), 1200);  // usable where an Engine& is expected
    EXPECT_EQ(get_priority(w), 7);
}

TEST(WithPriority, CompositionFlavorForFundamental) {
    auto w = with_priority(2, 42);
    EXPECT_EQ(w.priority(), 2);
    EXPECT_EQ(w.value(), 42);
    EXPECT_EQ(*w, 42);
    EXPECT_EQ(get_priority(w), 2);
}

TEST(WithPriority, MakePrioritizedSharedPtr) {
    const auto sp = make_prioritized<Engine>(9, 1500);
    ASSERT_NE(sp, nullptr);
    EXPECT_EQ(sp->priority(), 9);
    EXPECT_EQ(sp->value().rpm, 1500);

    // shared_ptr<WithPriority<Engine>> resolves its priority via get_priority.
    EXPECT_EQ(get_priority(sp), 9);

    // Usable with PrioritizedCompare through a shared_ptr<Prioritized>.
    const std::shared_ptr<Prioritized> as_base = sp;
    EXPECT_EQ(as_base->priority(), 9);
}

TEST(WithPriority, DefaultConstructibleWhenTIs) {
    WithPriority<int> w;  // guarded default ctor
    EXPECT_EQ(w.priority(), Prioritized::DEFAULT_PRIORITY);
    EXPECT_EQ(w.value(), 0);

    w.set_priority(5);
    EXPECT_EQ(w.priority(), 5);
}

}  // namespace
