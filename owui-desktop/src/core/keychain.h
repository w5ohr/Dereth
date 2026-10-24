// Session tokens live in the macOS keychain, never on disk in the clear.
#pragma once

#include <string>

namespace keychain {

bool save(const std::string& account, const std::string& secret);
std::string load(const std::string& account);
void erase(const std::string& account);

}  // namespace keychain
