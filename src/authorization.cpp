#include "authorization.hpp"

#include <stdexcept>
#include <string>

namespace rrs {
namespace {

constexpr char kAuthenticationRequired[] = "authentication required";
constexpr char kRoleNotAuthorized[] = "role is not authorized for this operation";
constexpr char kNotProtected[] = "operation is not protected";

bool is_protected(MenuAction action) {
    return action != MenuAction::Register && action != MenuAction::Login &&
           action != MenuAction::Exit;
}

MenuTree role_menu(Role role) {
    return role == Role::Administrator ? MenuTree::Administrator : MenuTree::Passenger;
}

}  // namespace

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

Outcome AuthorizationModule::authorize(const Session* session, MenuAction action) {
    if (!is_protected(action)) {
        return Outcome::refusal(kNotProtected);
    }
    if (session == nullptr) {
        return Outcome::refusal(kAuthenticationRequired);
    }
    if (!is_available(role_menu(session->role), action)) {
        return Outcome::refusal(kRoleNotAuthorized);
    }
    return Outcome::success("authorized");
}

Outcome AuthorizationModule::invoke(const Session* session, MenuAction action,
                                    const ActionHandler& protected_operation) {
    const auto result = authorize(session, action);
    if (result.kind != OutcomeKind::Success) {
        return result;
    }
    return protected_operation(action);
}

}  // namespace rrs
