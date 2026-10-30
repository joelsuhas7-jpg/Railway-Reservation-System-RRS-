#include "auth_flow.hpp"

#include <exception>
#include <istream>
#include <ostream>

namespace rrs {
namespace {

bool read_field(std::istream& in, std::ostream& out, const char* label, std::string& value) {
    out << label << ": " << std::flush;
    if (!std::getline(in, value)) {
        out << '\n';
        return false;
    }
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }
    return true;
}

}  // namespace

std::optional<AccountForm> read_account_form(std::istream& in, std::ostream& out) {
    AccountForm form;
    if (!read_field(in, out, "User id", form.user_id) ||
        !read_field(in, out, "Password", form.password) ||
        !read_field(in, out, "Contact", form.contact)) {
        return std::nullopt;
    }
    return form;
}

Outcome to_outcome(const AuthResult& result, const std::string& record_id) {
    switch (result.status) {
        case AuthStatus::Ok:
            return Outcome::success(result.message, record_id);
        case AuthStatus::InvalidInput:
            return Outcome::validation_failure(result.field, result.message);
        case AuthStatus::Refused:
            break;
    }
    return Outcome::refusal(result.message);
}

bool run_bootstrap(std::istream& in, std::ostream& out, std::ostream& err,
                   const UserStore& users) {
    if (!bootstrap_required(users)) {
        return true;
    }
    out << "First run: create the administrator account.\n";
    while (true) {
        const auto form = read_account_form(in, out);
        if (!form) {
            return false;
        }
        const auto result = bootstrap_admin(users, form->user_id, form->password, form->contact);
        report_outcome(out, err, to_outcome(result, form->user_id));
        if (result) {
            return true;
        }
    }
}

Outcome register_from_console(std::istream& in, std::ostream& out, const UserStore& users) {
    const auto form = read_account_form(in, out);
    if (!form) {
        return Outcome::refusal("registration cancelled");
    }
    try {
        return to_outcome(register_passenger(users, form->user_id, form->password, form->contact),
                          form->user_id);
    } catch (const std::exception& error) {
        return Outcome::system_failure(error.what());
    }
}

}  // namespace rrs
