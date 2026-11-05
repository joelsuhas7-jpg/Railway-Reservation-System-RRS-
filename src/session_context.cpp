#include "session_context.hpp"

#include <stdexcept>
#include <utility>

#include "auth_flow.hpp"

namespace rrs {

std::string AppContext::now_timestamp() const {
    return timestamp ? timestamp() : utc_timestamp_now();
}

void MenuRouter::add(MenuAction action, SessionHandler handler) {
    if (!handler) {
        throw std::logic_error("menu handler must not be empty");
    }
    if (!handlers_.emplace(action, std::move(handler)).second) {
        throw std::logic_error("menu action already has a handler");
    }
}

bool MenuRouter::handles(MenuAction action) const {
    return handlers_.count(action) != 0;
}

Outcome MenuRouter::dispatch(MenuAction action, const AppContext& context, const Session& session,
                             const SessionActivityCheck& activity) const {
    const auto handler = handlers_.find(action);
    if (handler == handlers_.end()) {
        return Outcome::refusal("this feature is not available yet");
    }
    return handler->second(context, session, activity);
}

}  // namespace rrs
