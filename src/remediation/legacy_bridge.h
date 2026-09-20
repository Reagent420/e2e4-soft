#pragma once

#include "remediation/backup_store.h"
#include "remediation/windows_state_api.h"

#include <string>

namespace gno {
namespace remediation {

// Routes a legacy fix_action id ("power_plan", "game_dvr", "tcp", "dns", "mtu",
// "priority") through the transactional engine: backup -> apply -> verify.
// Returns a human-readable UTF-8 result. Empty string = id not covered here,
// caller should fall back to its legacy implementation.
//
// applied_out (optional) distinguishes "изменение реально применено" from
// отказ/подсказку. Без этого вызывающий код вынужден угадывать по тексту
// сообщения, и автопилот сообщал об успехе даже когда движок ничего не сделал.
std::string applySafeFix(const std::string& legacy_id, WindowsStateApi& api, IBackupStore& store,
                         bool* applied_out = nullptr);

// Применяет конкретное DNS-значение через транзакционный движок
// (backup → apply → verify → rollback). В отличие от applySafeFix("dns"),
// который ставит пару движка 1.1.1.1/1.0.0.1, здесь — то значение, что выбрал
// пользователь в таблице DNS. Адреса должны быть отвалидированы вызывающим
// (IPv4), иначе netsh-команда будет собрана из мусора.
//
// value: DnsValue{false, {"a","b"}} — статическая пара; DnsValue{true, {}} —
// сброс на DHCP.
std::string applySafeDns(const DnsValue& value, WindowsStateApi& api,
                         IBackupStore& store, bool* applied_out = nullptr);

} // namespace remediation
} // namespace gno
