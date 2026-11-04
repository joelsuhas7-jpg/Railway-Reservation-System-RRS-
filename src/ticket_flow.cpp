#include "ticket_flow.hpp"

#include <cctype>
#include <string>

#include "booking_flow.hpp"
#include "console_input.hpp"
#include "persistence.hpp"
#include "ticket_retrieval.hpp"

namespace rrs {

Outcome view_ticket_from_console(const AppContext& context, const Session& session,
                                 const SessionActivityCheck& session_activity) {
    const auto expired = [&session_activity] {
        return session_activity && !session_activity(false);
    };
    std::string pnr;
    if (expired() || !read_field(context.in, context.out, "PNR", pnr) ||
        (session_activity && !session_activity(true))) {
        return expired() ? Outcome::refusal(std::string(kSessionExpiredMessage))
                         : Outcome::refusal("ticket view cancelled");
    }
    for (auto& character : pnr) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }

    const TicketRetrievalModule tickets{PersistenceStore{context.users.data_directory()},
                                        context.audit_log};
    const auto lookup = tickets.retrieve(session, context.now_timestamp(), pnr);
    if (lookup.booking) {
        render_ticket(context.out, *lookup.booking);
    }
    return lookup.outcome;
}

void register_ticket_flows(MenuRouter& router) {
    router.add(MenuAction::ViewTicket, view_ticket_from_console);
}

}  // namespace rrs
