#include "pluginsignature.h"

#include <cstring>
#include <map>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

#include "core/zip.h"

extern "C" {
#include "tweetnacl.h"
}

namespace {

// TweetNaCl asks for its random numbers here, for new keys only. The seed
// of keysFromSeed() is given to it this way.
thread_local QByteArray g_seed;

constexpr char kFirstLine[] = "TuxAPE plugin manifest 1\n";

// The manifest's lines, sorted by path.
QByteArray manifestOf(const std::map<QString, QByteArray>& hashes)
{
    QByteArray text(kFirstLine);
    for (const auto& [path, hash] : hashes)
        text += hash.toHex() + "  " + path.toUtf8() + '\n';
    return text;
}

}  // namespace

extern "C" void randombytes(unsigned char* bytes, unsigned long long count)
{
    if (static_cast<unsigned long long>(g_seed.size()) == count) {
        std::memcpy(bytes, g_seed.constData(), count);
        return;
    }
    for (unsigned long long i = 0; i < count; ++i)
        bytes[i] = static_cast<unsigned char>(QRandomGenerator::system()->generate());
}

namespace pluginsign {

std::optional<QByteArray> folderManifest(const QString& folder)
{
    // From the folder's full path: a relative one would be taken from it.
    const QDir root(QFileInfo(folder).absoluteFilePath());
    if (!root.exists())
        return std::nullopt;
    std::map<QString, QByteArray> hashes;
    QDirIterator it(root.absolutePath(), QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = root.relativeFilePath(it.next());
        if (path == kSignatureFile)
            continue;
        QFile file(root.filePath(path));
        if (!file.open(QIODevice::ReadOnly))
            return std::nullopt;
        QCryptographicHash hash(QCryptographicHash::Sha512);
        if (!hash.addData(&file))
            return std::nullopt;
        hashes[path] = hash.result();
    }
    return manifestOf(hashes);
}

std::optional<QByteArray> archiveManifest(const tuxape::ZipArchive& zip, const QString& prefix)
{
    std::map<QString, QByteArray> hashes;
    for (const tuxape::ZipArchive::Entry& entry : zip.entries()) {
        const QString name = QString::fromStdString(entry.name);
        if (!name.startsWith(prefix) || name.endsWith('/'))
            continue;
        const QString path = name.mid(prefix.size());
        if (path == kSignatureFile)
            continue;
        const auto bytes = zip.read(entry);
        if (!bytes)
            return std::nullopt;
        hashes[path] = QCryptographicHash::hash(
            QByteArrayView(reinterpret_cast<const char*>(bytes->data()), static_cast<qsizetype>(bytes->size())),
            QCryptographicHash::Sha512);
    }
    return manifestOf(hashes);
}

std::optional<Signature> parse(const QByteArray& json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject())
        return std::nullopt;
    const QJsonObject object = document.object();
    if (object.value("format").toInt() != 1)
        return std::nullopt;
    Signature signature;
    signature.signer = object.value("signer").toString().trimmed();
    signature.key = QByteArray::fromBase64(object.value("key").toString().toLatin1());
    signature.signature = QByteArray::fromBase64(object.value("signature").toString().toLatin1());
    if (signature.signer.isEmpty() || signature.key.size() != kKeySize || signature.signature.size() != kSignatureSize)
        return std::nullopt;
    return signature;
}

QByteArray toJson(const Signature& signature)
{
    QJsonObject object;
    object["format"] = 1;
    object["signer"] = signature.signer;
    object["key"] = QString::fromLatin1(signature.key.toBase64());
    object["signature"] = QString::fromLatin1(signature.signature.toBase64());
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

bool verify(const Signature& signature, const QByteArray& manifest)
{
    if (signature.key.size() != kKeySize || signature.signature.size() != kSignatureSize)
        return false;
    // TweetNaCl checks a signed message: the signature, then what it signs.
    const QByteArray signedText = signature.signature + manifest;
    QByteArray opened(signedText.size(), '\0');
    unsigned long long length = 0;
    return crypto_sign_open(reinterpret_cast<unsigned char*>(opened.data()), &length,
                            reinterpret_cast<const unsigned char*>(signedText.constData()),
                            static_cast<unsigned long long>(signedText.size()),
                            reinterpret_cast<const unsigned char*>(signature.key.constData()))
           == 0;
}

KeyPair keysFromSeed(const QByteArray& seed)
{
    KeyPair keys;
    if (seed.size() != 32)
        return keys;
    keys.secret.resize(kSecretSize);
    keys.key.resize(kKeySize);
    g_seed = seed;
    crypto_sign_keypair(reinterpret_cast<unsigned char*>(keys.key.data()), reinterpret_cast<unsigned char*>(keys.secret.data()));
    g_seed.fill('\0');
    g_seed.clear();
    return keys;
}

KeyPair newKeys()
{
    QByteArray seed(32, '\0');
    for (char& byte : seed)
        byte = static_cast<char>(QRandomGenerator::system()->generate());
    KeyPair keys = keysFromSeed(seed);
    seed.fill('\0');
    return keys;
}

QByteArray sign(const QByteArray& manifest, const QByteArray& secret)
{
    if (secret.size() != kSecretSize)
        return {};
    QByteArray signedText(manifest.size() + kSignatureSize, '\0');
    unsigned long long length = 0;
    crypto_sign(reinterpret_cast<unsigned char*>(signedText.data()), &length,
                reinterpret_cast<const unsigned char*>(manifest.constData()), static_cast<unsigned long long>(manifest.size()),
                reinterpret_cast<const unsigned char*>(secret.constData()));
    return signedText.left(kSignatureSize);
}

QString fingerprint(const QByteArray& key)
{
    const QByteArray hex = QCryptographicHash::hash(key, QCryptographicHash::Sha256).left(16).toHex().toUpper();
    QStringList groups;
    for (int at = 0; at < hex.size(); at += 4)
        groups << QString::fromLatin1(hex.mid(at, 4));
    return groups.join(' ');
}

QString digest(const QByteArray& manifest)
{
    return QString::fromLatin1(QCryptographicHash::hash(manifest, QCryptographicHash::Sha256).toHex());
}

}  // namespace pluginsign
