// Tour of comms::SemVer and comms::VersionConstraint: parse versions, sort them
// in full SemVer 2.0.0 precedence order (prerelease ranks below release, numeric
// identifiers compare numerically), format them, then check a few npm-style range
// constraints. JSON-free, so it builds in the base config.

#include <commons/semver.hpp>
#include <commons/version_constraint.hpp>

#include <algorithm>
#include <format>
#include <iostream>
#include <vector>

int main() {
    namespace c = comms;

    // Parse: lenient partial cores, a `v` prefix, prerelease and +build.
    std::cout << "parse 1.2     : " << *c::SemVer::parse("1.2") << "  (patch defaults to 0)\n";
    std::cout << "parse v2.0.0  : " << *c::SemVer::parse("v2.0.0") << "\n";
    std::cout << "parse +build  : " << *c::SemVer::parse("1.2.3+build.7")
              << "  (build kept in text, ignored in ordering)\n";

    // Sort a mixed bag. The full §11 precedence puts prereleases before the
    // release and orders numeric identifiers numerically (alpha.2 < alpha.10).
    std::vector<c::SemVer> versions;
    for (const auto* s : {"1.0.0",
                          "1.0.0-beta",
                          "1.0.0-alpha.10",
                          "1.0.0-alpha.2",
                          "1.0.0-alpha.1",
                          "1.0.0-alpha",
                          "2.0.0",
                          "1.2.0"}) {
        versions.push_back(*c::SemVer::parse(s));
    }
    std::ranges::sort(versions);

    std::cout << "\nsorted ascending:\n";
    for (const auto& v : versions) {
        std::cout << "  " << std::format("{}", v) << "\n";
    }

    // Range constraints: intersection, caret, and tilde.
    const c::SemVer probe = *c::SemVer::parse("1.2.5");
    for (const auto* range : {">=1.2.0 <2.0.0", "^1.2.3", "~1.2.3", "^2.0.0"}) {
        const auto constraint = c::VersionConstraint::parse(range);
        std::cout << "\n"
                  << std::format("{}", probe) << " satisfies " << constraint << " ? "
                  << (constraint.satisfies(probe) ? "yes" : "no");
    }
    std::cout << "\n";

    return 0;
}
