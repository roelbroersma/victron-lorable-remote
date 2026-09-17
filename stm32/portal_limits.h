#pragma once
// Shared STM32/ESP limits. Include encoded form expansion, ten full GATT
// functions and four OTAA profiles; leave room for the synthetic HTTP header.
#define LORABLE_HTTP_BODY_MAX 7800
#define LORABLE_HTTP_BUFFER_SIZE 8192
#define LORABLE_HTTP_ASSEMBLY_MS 30000UL
