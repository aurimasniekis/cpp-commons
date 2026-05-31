// Tour of the comms::AuditRecord family: a shared audit-trail value type
// (who/what/when/from where), its ChangeAuditRecord<T> before/after extension,
// and the capped AuditLog<Record> collection (with the AuditRecords /
// ChangeAuditRecords<T> aliases). JSON-free, so it builds in the base library.

#include <commons/audit_record.hpp>

#include <iostream>
#include <string>

// Strong-typed ids: the templated helpers capture comms::to_string(id), so a
// heterogeneous record can still reference ids of different kinds.
COMMONS_DEFINE_UINT64_ID(OrderId, "order");
COMMONS_DEFINE_STRING_ID(TenantId, "tenant");

int main() {
    namespace c = comms;

    // A base record: username is the only required field; timestamp defaults to
    // now(); the rest are optional / empty until set.
    c::AuditRecord rec;
    rec.username = "alice";
    rec.ip = "192.0.2.1";
    rec.user_agent = "curl/8.0";
    rec.set_session_id(OrderId{42U});             // -> session_id "42"
    rec.add_related_id("order", OrderId{1001U});  // name -> id string
    rec.add_related_id("tenant", TenantId{"acme"});
    rec.metadata["action"] = c::md::Value{"checkout"};

    std::cout << "actor        : " << rec.username << "\n";
    std::cout << "session_id   : " << rec.session_id.value_or("(none)") << "\n";
    std::cout << "related order: " << rec.related_ids.at("order") << "\n";
    std::cout << "related tenant: " << rec.related_ids.at("tenant") << "\n";

    // A ChangeAuditRecord<T> adds before/after. A create has no `before`, a
    // delete has no `after`; an update sets both.
    c::ChangeAuditRecord<std::string> update;
    update.username = "bob";
    update.add_related_id("order", OrderId{1001U});  // inherited helper
    update.before = std::string{"pending"};
    update.after = std::string{"shipped"};
    std::cout << "status change: " << update.before.value_or("-") << " -> "
              << update.after.value_or("-") << "\n";

    // An AuditLog is capped (drop-oldest, FIFO). Push beyond the capacity and
    // the oldest records fall off the front.
    c::AuditRecords log{2};  // hold at most 2
    for (const auto* who : {"alice", "bob", "carol"}) {
        c::AuditRecord r;
        r.username = who;
        log.push(std::move(r));
    }
    std::cout << "log size     : " << log.size() << " (cap " << log.capacity() << ")\n";
    std::cout << "oldest kept  : " << log.front().username << "\n";  // bob (alice dropped)
    std::cout << "newest       : " << log.back().username << "\n";   // carol

    // The default capacity comes from COMMONS_AUDIT_RECORDS_CAPACITY (a
    // build/config value-override seam, default 3).
    const c::ChangeAuditRecords<int> counters;
    std::cout << "default cap  : " << counters.capacity() << "\n";

    return 0;
}
