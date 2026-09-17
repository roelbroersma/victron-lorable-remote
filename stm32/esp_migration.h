#pragma once
#include "config_store.h"
#ifdef ESP_MIGRATION_TO_NATIVE
void migrationBegin(const RuntimeConfig &config);
// True while the explicitly commanded bootstrap owns UART1. No auto-install.
bool migrationService();
#endif
