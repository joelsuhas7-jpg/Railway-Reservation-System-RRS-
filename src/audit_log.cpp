#include "audit_log.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace rrs {
namespace {

void validate_field(std::string_view field_name, std::string_view value) {
    if (value.empty()) {
        throw std::invalid_argument(std::string(field_name) + " must not be empty");
    }

    for (const unsigned char character : value) {
        if (character == '\t' || character == '\n' ||
            character == '\r' || character == '\0') {
            throw std::invalid_argument(
                std::string(field_name) + " contains an invalid control character");
        }
    }
}

void validate_entry(const AuditEntry& entry) {
    validate_field("timestamp", entry.timestamp);
    validate_field("actor", entry.actor);
    validate_field("role", entry.role);
    validate_field("action", entry.action);
    validate_field("record_id", entry.record_id);
}

}  // namespace

AuditLog::AuditLog(std::filesystem::path log_path)
    : log_path_(std::move(log_path)) {
    if (log_path_.empty()) {
        throw std::invalid_argument("audit log path must not be empty");
    }
}

void AuditLog::append(const AuditEntry& entry) const {
    validate_entry(entry);

    std::string line;
    line.reserve(entry.timestamp.size() + entry.actor.size() +
                 entry.role.size() + entry.action.size() +
                 entry.record_id.size() + 5);

    line += entry.timestamp;
    line += '\t';
    line += entry.actor;
    line += '\t';
    line += entry.role;
    line += '\t';
    line += entry.action;
    line += '\t';
    line += entry.record_id;
    line += '\n';

    // Append mode prevents overwriting existing audit entries.
    std::ofstream output(log_path_, std::ios::binary | std::ios::out | std::ios::app);

    if (!output) {
        throw std::runtime_error("cannot open audit log for appending");
    }

    output.write(line.data(), static_cast<std::streamsize>(line.size()));
    output.flush();

    if (!output) {
        throw std::runtime_error("failed to append audit entry");
    }
}

}  // namespace rrs
