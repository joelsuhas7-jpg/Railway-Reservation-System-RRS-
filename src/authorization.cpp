#include "authorization.hpp"

#include <stdexcept>
#include <string>

namespace rrs {

AuthorizationResult AuthorizationModule::require_administrator(
    const Session& session, std::string_view operation, std::string_view timestamp,
    const AuditLog& audit_log) {
    if (operation.empty()) {
        throw std::invalid_argument("administrator operation must not be empty");
    }

    if (session.role == Role::Administrator) {
        return {true, "authorized"};
    }

    audit_log.append({std::string(timestamp), session.user_id, "PASSENGER",
                      "ACCESS_DENIED_" + std::string(operation), "-"});
    return {false, "administrator role required"};
}

}  // namespace rrs
