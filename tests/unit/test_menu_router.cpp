#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>

#include "audit_log.hpp"
#include "auth_flow.hpp"
#include "persistence.hpp"
#include "session_context.hpp"

namespace {

struct Fixture {
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    // Nothing is written: the handlers under test never touch storage.
    rrs::UserStore users{rrs::PersistenceStore{std::filesystem::temp_directory_path()}};
    rrs::AuditLog audit_log{std::filesystem::temp_directory_path() / "rrs-router-unused.log"};
    rrs::AppContext context{in, out, err, users, audit_log, {}, {}};
    rrs::Session session{"passenger1", rrs::Role::Passenger};
};

}  // namespace

TEST_CASE("ENB-08-01: the router runs the handler registered for an action", "[unit][M17]") {
    Fixture fixture;
    rrs::MenuRouter router;
    bool called = false;
    router.add(rrs::MenuAction::SearchTrains,
               [&called](const rrs::AppContext&, const rrs::Session& session,
                         const rrs::SessionActivityCheck&) {
                   called = true;
                   return rrs::Outcome::success("searched", session.user_id);
               });

    const auto outcome = router.dispatch(rrs::MenuAction::SearchTrains, fixture.context,
                                         fixture.session, {});

    CHECK(called);
    CHECK(router.handles(rrs::MenuAction::SearchTrains));
    CHECK(outcome.kind == rrs::OutcomeKind::Success);
    CHECK(outcome.detail == "passenger1");
}

TEST_CASE("ENB-08-02: an action without a handler is refused as not available yet",
          "[unit][M17]") {
    Fixture fixture;
    const rrs::MenuRouter router;

    const auto outcome =
        router.dispatch(rrs::MenuAction::BookTicket, fixture.context, fixture.session, {});

    CHECK_FALSE(router.handles(rrs::MenuAction::BookTicket));
    CHECK(outcome.kind == rrs::OutcomeKind::BusinessRuleRefusal);
    CHECK(outcome.message == "this feature is not available yet");
}

TEST_CASE("ENB-08-03: duplicate and empty registrations are rejected", "[unit][M17]") {
    rrs::MenuRouter router;
    const rrs::SessionHandler handler = [](const rrs::AppContext&, const rrs::Session&,
                                           const rrs::SessionActivityCheck&) {
        return rrs::Outcome::success("ok");
    };
    router.add(rrs::MenuAction::ViewTicket, handler);

    CHECK_THROWS_AS(router.add(rrs::MenuAction::ViewTicket, handler), std::logic_error);
    CHECK_THROWS_AS(router.add(rrs::MenuAction::CancelTicket, rrs::SessionHandler{}),
                    std::logic_error);
}

TEST_CASE("ENB-08-04: each feature registers its own actions without clashing", "[unit][M17]") {
    rrs::MenuRouter router;
    rrs::register_account_flows(router);

    CHECK(router.handles(rrs::MenuAction::ChangePassword));
    CHECK(router.handles(rrs::MenuAction::ResetPassword));
    CHECK_FALSE(router.handles(rrs::MenuAction::Logout));
    CHECK_THROWS_AS(rrs::register_account_flows(router), std::logic_error);
}

TEST_CASE("ENB-08-05: the context falls back to the UTC clock without a timestamp provider",
          "[unit][M17]") {
    Fixture fixture;
    const auto stamp = fixture.context.now_timestamp();

    REQUIRE(stamp.size() == 20);
    CHECK(stamp[4] == '-');
    CHECK(stamp[10] == 'T');
    CHECK(stamp.back() == 'Z');
}
