#pragma once

#include "authentication.hpp"
#include "console_ui.hpp"

namespace rrs {

// M5 AuthorizationModule: protects every non-public menu operation. A null session represents
// an unauthenticated caller and is always refused for protected actions.
class AuthorizationModule final {
public:
    static Outcome authorize(const Session* session, MenuAction action);
    static Outcome invoke(const Session* session, MenuAction action,
                          const ActionHandler& protected_operation);
};

}  // namespace rrs
