#pragma once

#include "tag_ndef.hpp"

bool decodeTag(const std::vector<NdefRecord>& records, TagHints& out);
