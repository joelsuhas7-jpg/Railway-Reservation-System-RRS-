#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace rrs {

struct AuditEntry {
    std::string timestamp;
    std::string actor;
    std::string role;
    std::string action;
    std::string record_id;
};

class AuditLog {
public:
    explicit AuditLog(std::filesystem::path log_path);

    // Appends exactly one entry. Existing entries cannot be edited or deleted.
    void append(const AuditEntry& entry) const;

private:
    std::filesystem::path log_path_;
};

}  // namespace rrs
