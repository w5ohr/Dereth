#import <CommonCrypto/CommonCrypto.h>
#import <Security/Security.h>

#include "privatestore.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

#include "common.h"

namespace privatestore {
namespace {

constexpr char kMagic[4] = {'O', 'W', 'P', '1'};
constexpr size_t kIvSize = kCCBlockSizeAES128;
constexpr size_t kTagSize = CC_SHA256_DIGEST_LENGTH;

std::string hmac(const std::string& key, const std::string& data) {
  unsigned char out[CC_SHA256_DIGEST_LENGTH];
  CCHmac(kCCHmacAlgSHA256, key.data(), key.size(), data.data(), data.size(), out);
  return std::string(reinterpret_cast<const char*>(out), sizeof(out));
}

// Separate keys for the cipher and the tag, derived from the stored one.
std::string encKey(const std::string& key) { return hmac(key, "owui-private-enc"); }
std::string macKey(const std::string& key) { return hmac(key, "owui-private-mac"); }

bool constantTimeEqual(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  return diff == 0;
}

std::string pathFor(const std::string& dir, const std::string& id) { return dir + "/" + id + ".enc"; }

}  // namespace

std::string randomBytes(size_t count) {
  std::string out(count, '\0');
  if (SecRandomCopyBytes(kSecRandomDefault, count, out.data()) != errSecSuccess) {
    // Should not happen; never return predictable bytes.
    out.clear();
  }
  return out;
}

std::string seal(const std::string& plaintext, const std::string& key) {
  if (key.size() != 32) return "";
  const std::string iv = randomBytes(kIvSize);
  if (iv.size() != kIvSize) return "";
  const std::string ek = encKey(key);
  std::string cipher(plaintext.size() + kCCBlockSizeAES128, '\0');
  size_t written = 0;
  const CCCryptorStatus status =
      CCCrypt(kCCEncrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding, ek.data(), kCCKeySizeAES256, iv.data(),
              plaintext.data(), plaintext.size(), cipher.data(), cipher.size(), &written);
  if (status != kCCSuccess) return "";
  cipher.resize(written);
  std::string blob(kMagic, sizeof(kMagic));
  blob += iv;
  blob += cipher;
  blob += hmac(macKey(key), blob);
  return blob;
}

bool open(const std::string& blob, const std::string& key, std::string* plaintext) {
  if (key.size() != 32 || blob.size() < sizeof(kMagic) + kIvSize + kTagSize) return false;
  if (std::memcmp(blob.data(), kMagic, sizeof(kMagic)) != 0) return false;
  const std::string body = blob.substr(0, blob.size() - kTagSize);
  const std::string tag = blob.substr(blob.size() - kTagSize);
  if (!constantTimeEqual(tag, hmac(macKey(key), body))) return false;
  const std::string iv = body.substr(sizeof(kMagic), kIvSize);
  const std::string cipher = body.substr(sizeof(kMagic) + kIvSize);
  const std::string ek = encKey(key);
  std::string out(cipher.size() + kCCBlockSizeAES128, '\0');
  size_t written = 0;
  const CCCryptorStatus status =
      CCCrypt(kCCDecrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding, ek.data(), kCCKeySizeAES256, iv.data(),
              cipher.data(), cipher.size(), out.data(), out.size(), &written);
  if (status != kCCSuccess) return false;
  out.resize(written);
  *plaintext = out;
  return true;
}

bool save(const std::string& dir, const std::string& key, const std::string& id, const Json& doc) {
  mkdir(dir.c_str(), 0700);
  const std::string blob = seal(doc.dump(), key);
  if (blob.empty()) return false;
  // Write beside, then rename: a crash mid-write never loses the old copy.
  const std::string tmp = pathFor(dir, id) + ".tmp";
  if (!core::writeFile(tmp, blob)) return false;
  chmod(tmp.c_str(), 0600);
  return rename(tmp.c_str(), pathFor(dir, id).c_str()) == 0;
}

std::optional<Json> load(const std::string& dir, const std::string& key, const std::string& id) {
  bool ok = false;
  const std::string blob = core::readFile(pathFor(dir, id), &ok);
  if (!ok) return std::nullopt;
  std::string plain;
  if (!open(blob, key, &plain)) return std::nullopt;
  Json doc = Json::parse(plain, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) return std::nullopt;
  return doc;
}

bool remove(const std::string& dir, const std::string& id) { return unlink(pathFor(dir, id).c_str()) == 0; }

std::vector<Entry> list(const std::string& dir, const std::string& key) {
  std::vector<Entry> out;
  DIR* d = opendir(dir.c_str());
  if (!d) return out;
  while (dirent* entry = readdir(d)) {
    const std::string name = entry->d_name;
    if (name.size() < 5 || name.substr(name.size() - 4) != ".enc") continue;
    const std::string id = name.substr(0, name.size() - 4);
    if (!isPrivateId(id)) continue;
    std::optional<Json> doc = load(dir, key, id);
    if (!doc) continue;
    Entry e;
    e.id = id;
    e.title = doc->value("title", "Private chat");
    e.updatedAt = doc->value("updated_at", static_cast<int64_t>(0));
    out.push_back(e);
  }
  closedir(d);
  std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.updatedAt > b.updatedAt; });
  return out;
}

bool isPrivateId(const std::string& id) { return id.rfind("local-", 0) == 0; }

std::string newId() { return "local-" + core::uuid(); }

}  // namespace privatestore
