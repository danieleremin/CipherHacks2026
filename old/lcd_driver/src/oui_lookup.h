// firmware/lcd-driver/src/oui_lookup.h
// Binary search over the SD-resident OUI table (built by tools/oui_builder.py),
// fronted by a small in-RAM LRU cache to cut repeat SD seeks.

#pragma once
#include <stdint.h>

bool oui_lookup_init();

// Returns true if found; writes name (null-terminated) into buf (must be >= 24 bytes)
bool oui_lookup(const uint8_t* mac, char* buf);
