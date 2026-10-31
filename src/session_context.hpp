#pragma once

#include <chrono>
#include <functional>
#include <iosfwd>
#include <map>
#include <string>
#include <utility>

#include "audit_log.hpp"
#include "authentication.hpp"
#include "console_ui.hpp"

namespace rrs {

// Trusted ISO 8601 UTC timestamp source for audit entries, e.g. "2026-10-30T18:30:00Z".
using AuditTimestampProvider = std::function<std::string()>;
// Monotonic clock seam for deterministic idle-timeout tests.
using SessionClockProvider = std::function<std::chrono::steady_clock::time_point()>;
// Checks expiry; pass true after a user input line is accepted to mark activity. Returns false
// once the session has expired.
using SessionActivityCheck = std::function<bool(bool)>;

// Everything a console flow needs from the running application.
struct AppContext {
    AppContext(std::istream& input, std::ostream& output, std::ostream& error,
               const UserStore& user_store, const AuditLog& audit, AuditTimestampProvider stamp = {},
               SessionClockProvider session_clock = {})
        : in(input), out(output), err(error), users(user_store), audit_log(audit),
          timestamp(std::move(stamp)), clock(std::move(session_clock)) {}

    std::istream& in;
    std::ostream& out;
    std::ostream& err;
    const UserStore& users;
    const AuditLog& audit_log;
    AuditTimestampProvider timestamp;  // empty means utc_timestamp_now()
    SessionClockProvider clock;        // empty means std::chrono::steady_clock::now()

    std::string now_timestamp() const;
};

// Handles one authorised role-menu action for a logged-in session.
using SessionHandler =
    std::function<Outcome(const AppContext&, const Session&, const SessionActivityCheck&)>;

// Maps role-menu actions to the flows that serve them. Each feature registers its own actions,
// so adding a feature never edits another feature's dispatch code.
class MenuRouter {
public:
    // Throws std::logic_error if the action already has a handler.
    void add(MenuAction action, SessionHandler handler);

    bool handles(MenuAction action) const;

    // Runs the registered handler; an action without one is refused as not available yet.
    Outcome dispatch(MenuAction action, const AppContext& context, const Session& session,
                     const SessionActivityCheck& activity) const;

private:
    std::map<MenuAction, SessionHandler> handlers_;
};

}  // namespace rrs
