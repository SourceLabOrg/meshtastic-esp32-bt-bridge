#pragma once

#include <Arduino.h>

// Escape special characters for safe inclusion in JSON strings
String utils_escape_json(const String& input);
