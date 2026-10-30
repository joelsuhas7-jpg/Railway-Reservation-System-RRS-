#pragma once

#include <string>
#include <string_view>

#include "authentication.hpp"
#include "audit_log.hpp"
#include "console_ui.hpp"

namespace rrs {

// Result of checking whether a session may invoke an administrator operation.
struct AuthorizationResult {
    bool allowed{false};
    std::string message;

    explicit operator bool() const noexcept { return allowed; }
};

// M5 AuthorizationModule: checks the role at the operation boundary. A denied
// administrator attempt is appended to the audit log before this function returns.
class AuthorizationModule final {
public:
    static AuthorizationResult require_administrator(const Session& session,
                                                      std::string_view operation,
                                                      std::string_view timestamp,
                                                      const AuditLog& audit_log);

    // Protects every non-public menu operation. A null session is unauthenticated.
    static Outcome authorize(const Session* session, MenuAction action);
    static Outcome invoke(const Session* session, MenuAction action,
                          const ActionHandler& protected_operation);
};

}  // namespace rrs
