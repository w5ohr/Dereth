#include "core/keychain.h"

#include "core/common.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

namespace keychain {
namespace {

constexpr const char* kService = "com.davidcochran.owui-desktop";

NSDictionary* baseQuery(const std::string& account) {
  return @{
    (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
    (__bridge id)kSecAttrService : [NSString stringWithUTF8String:kService],
    (__bridge id)kSecAttrAccount : [NSString stringWithUTF8String:account.c_str()],
  };
}

void trace(const char* op, const std::string& account) {
  core::debugLog("keychain: %s %s", op, account.c_str());
  if (std::getenv("OWUI_KEYCHAIN_TRACE")) {
    for (NSString* line in [NSThread callStackSymbols]) core::debugLog("  %s", line.UTF8String);
  }
}

}  // namespace

bool save(const std::string& account, const std::string& secret) {
  trace("save", account);
  erase(account);
  NSMutableDictionary* item = [baseQuery(account) mutableCopy];
  item[(__bridge id)kSecValueData] =
      [NSData dataWithBytes:secret.data() length:secret.size()];
  item[(__bridge id)kSecAttrAccessible] = (__bridge id)kSecAttrAccessibleAfterFirstUnlock;
  OSStatus status = SecItemAdd((__bridge CFDictionaryRef)item, nullptr);
  return status == errSecSuccess;
}

std::string load(const std::string& account) {
  trace("load", account);
  NSMutableDictionary* query = [baseQuery(account) mutableCopy];
  query[(__bridge id)kSecReturnData] = @YES;
  query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
  CFTypeRef result = nullptr;
  OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
  if (status != errSecSuccess || !result) return {};
  NSData* data = (__bridge_transfer NSData*)result;
  return std::string(static_cast<const char*>(data.bytes), data.length);
}

void erase(const std::string& account) {
  trace("erase", account);
  SecItemDelete((__bridge CFDictionaryRef)baseQuery(account));
}

}  // namespace keychain
