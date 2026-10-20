#pragma once

#include <optional>

#include <QByteArray>
#include <QString>

namespace tuxape {
class ZipArchive;
}

// The signatures of TuxAPE's plugins: who made a plugin, and that its files
// are still those its author made.
//
// A signed plugin has, beside its plugin.json, a signature.json:
//
//   {"format": 1, "signer": "Name", "key": "<public key>", "signature": "<signature>"}
//
// key and signature in base64. The signature is an Ed25519 one, made with
// the author's secret key, over the plugin's manifest: the line
// "TuxAPE plugin manifest 1", then, for each file of the plugin but
// signature.json, sorted by path, its SHA-512 in hexadecimal, two spaces
// and its path from the plugin's folder, with '/' between folders; every
// line ends with "\n". A file added, taken away or changed by a single byte
// gives another manifest, which the signature is not for.
//
// The Ed25519 functions are TweetNaCl's (tweetnacl.c, in the public
// domain); the files' SHA-512 is Qt's.
namespace pluginsign {

inline constexpr char kSignatureFile[] = "signature.json";
inline constexpr int kKeySize = 32;        // a public key
inline constexpr int kSecretSize = 64;     // a secret key: its seed, then its public key
inline constexpr int kSignatureSize = 64;

struct Signature {
    QString signer;        // the name the author signs with
    QByteArray key;        // the author's public key
    QByteArray signature;  // over the manifest
};

// The manifest of the plugin in a folder; or of the one in an archive,
// whose files are under `prefix` ("" or "name/"). Nothing if a file cannot
// be read.
std::optional<QByteArray> folderManifest(const QString& folder);
std::optional<QByteArray> archiveManifest(const tuxape::ZipArchive& zip, const QString& prefix);

// What a signature.json says; nothing if it is not one.
std::optional<Signature> parse(const QByteArray& json);
QByteArray toJson(const Signature& signature);

// Whether the signature was made with its key's secret over that manifest.
bool verify(const Signature& signature, const QByteArray& manifest);

// An author's keys. The secret one is to be kept by the author alone; the
// public one goes with what they sign.
struct KeyPair {
    QByteArray secret;
    QByteArray key;
};
// New keys, from the system's random numbers.
KeyPair newKeys();
// The keys that come from a 32-byte seed (RFC 8032's secret key).
KeyPair keysFromSeed(const QByteArray& seed);
// A signature of the manifest with a secret key; empty for a key that is
// not one.
QByteArray sign(const QByteArray& manifest, const QByteArray& secret);

// A key as a user is shown it: its SHA-256's first 16 bytes, in eight
// groups of four hexadecimal digits.
QString fingerprint(const QByteArray& key);
// A manifest as an approval keeps it: its SHA-256 in hexadecimal.
QString digest(const QByteArray& manifest);

}  // namespace pluginsign
