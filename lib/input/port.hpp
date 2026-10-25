#pragma once

#include <aurora/pad.hpp>

namespace aurora::pad::detail {

void initialize_profiles();
uint64_t profile_generation();
std::shared_ptr<const binding::BindingSet> resolve_port(uint32_t port);
std::shared_ptr<const binding::BindingSet> resolve_controller(input::SourceId source);
std::vector<binding::Binding> default_pad_bindings(input::SourceId source);
void migrate_legacy(const std::filesystem::path& path, std::vector<std::filesystem::path>& migrated,
                    LegacyMigration& report);
void initialize();
bool read(uint32_t port, PADStatus& status);
void set_virtual_status(uint32_t port, const PADStatus* status);
void set_blocked(bool blocked);
bool blocked();
bool consume_cancellation(uint32_t port);
bool captured(uint32_t port);
input::SourceId controller_source(uint32_t port);

} // namespace aurora::pad::detail
