// tuxape-plugin-sign: what a plugin's author signs it with.
//
//   tuxape-plugin-sign key SECRET-FILE "Name"
//       makes a pair of keys: the secret one goes into SECRET-FILE, which
//       only its owner may read, and the public one is printed. Name is the
//       one the plugins are signed with.
//   tuxape-plugin-sign sign PLUGIN-FOLDER SECRET-FILE
//       writes PLUGIN-FOLDER/signature.json, over every other file of the
//       folder: to be done last, just before the folder goes into its .zip.
//   tuxape-plugin-sign check PLUGIN-FOLDER|PLUGIN.zip
//       says who signed the plugin, and whether its files are still those
//       that were signed.
//
// The secret file holds the name and the secret key: keep it, and keep it
// to yourself. Lost, the plugins cannot be signed again with the same key;
// copied, anyone can sign in your name.

#include <cstdio>

#include <QCoreApplication>
#include <QFile>
#include <QFileDevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include "core/zip.h"
#include "pluginsignature.h"

namespace {

int fail(const QString& text)
{
    std::fprintf(stderr, "tuxape-plugin-sign: %s\n", qPrintable(text));
    return 1;
}

int usage()
{
    std::fprintf(stderr,
                 "usage: tuxape-plugin-sign key SECRET-FILE NAME\n"
                 "       tuxape-plugin-sign sign PLUGIN-FOLDER SECRET-FILE\n"
                 "       tuxape-plugin-sign check PLUGIN-FOLDER|PLUGIN.zip\n");
    return 2;
}

int makeKey(const QString& path, const QString& name)
{
    if (name.trimmed().isEmpty())
        return fail("the name the plugins are signed with is missing");
    if (QFile::exists(path))
        return fail(path + " is already there: it is not written over");
    const pluginsign::KeyPair keys = pluginsign::newKeys();
    QJsonObject object;
    object["signer"] = name.trimmed();
    object["secret"] = QString::fromLatin1(keys.secret.toBase64());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return fail("cannot write " + path);
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(object).toJson());
    file.close();
    std::printf("signer:      %s\npublic key:  %s\nfingerprint: %s\n", qPrintable(name.trimmed()),
                keys.key.toBase64().constData(), qPrintable(pluginsign::fingerprint(keys.key)));
    return 0;
}

int signFolder(const QString& folder, const QString& secretPath)
{
    QFile file(secretPath);
    if (!file.open(QIODevice::ReadOnly))
        return fail("cannot read " + secretPath);
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    const QString signer = object.value("signer").toString();
    const QByteArray secret = QByteArray::fromBase64(object.value("secret").toString().toLatin1());
    if (signer.isEmpty() || secret.size() != pluginsign::kSecretSize)
        return fail(secretPath + " is not a secret key made by tuxape-plugin-sign");
    if (!QFile::exists(folder + "/plugin.json"))
        return fail(folder + " has no plugin.json");
    QFile::remove(folder + '/' + pluginsign::kSignatureFile);
    const auto manifest = pluginsign::folderManifest(folder);
    if (!manifest)
        return fail("cannot read the files of " + folder);
    pluginsign::Signature signature;
    signature.signer = signer;
    signature.key = secret.right(pluginsign::kKeySize);
    signature.signature = pluginsign::sign(*manifest, secret);
    if (!pluginsign::verify(signature, *manifest))
        return fail("the signature made does not check");
    QFile out(folder + '/' + pluginsign::kSignatureFile);
    if (!out.open(QIODevice::WriteOnly))
        return fail("cannot write " + out.fileName());
    out.write(pluginsign::toJson(signature));
    std::printf("signed by %s (%s)\n", qPrintable(signer), qPrintable(pluginsign::fingerprint(signature.key)));
    return 0;
}

int check(const QString& path)
{
    std::optional<QByteArray> manifest;
    std::optional<pluginsign::Signature> signature;
    if (path.endsWith(".zip", Qt::CaseInsensitive)) {
        const auto zip = tuxape::ZipArchive::open(path.toStdString());
        if (!zip)
            return fail(path + " is not a .zip archive");
        // The plugin's folder: where plugin.json is.
        QString prefix;
        for (const auto& entry : zip->entries()) {
            const QString name = QString::fromStdString(entry.name);
            if (name == "plugin.json" || (name.count('/') == 1 && name.endsWith("/plugin.json")))
                prefix = name.left(name.size() - 11);
        }
        manifest = pluginsign::archiveManifest(*zip, prefix);
        if (const auto json = zip->read((prefix + pluginsign::kSignatureFile).toStdString()))
            signature = pluginsign::parse(QByteArray(reinterpret_cast<const char*>(json->data()), static_cast<qsizetype>(json->size())));
    } else {
        manifest = pluginsign::folderManifest(path);
        QFile file(path + '/' + pluginsign::kSignatureFile);
        if (file.open(QIODevice::ReadOnly))
            signature = pluginsign::parse(file.readAll());
    }
    if (!manifest)
        return fail("cannot read the files of " + path);
    if (!signature) {
        std::printf("not signed\n");
        return 1;
    }
    const bool good = pluginsign::verify(*signature, *manifest);
    std::printf("signed by %s (%s): %s\n", qPrintable(signature->signer), qPrintable(pluginsign::fingerprint(signature->key)),
                good ? "the files are those that were signed" : "the files are NOT those that were signed");
    return good ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.size() == 3 && args[0] == "key")
        return makeKey(args[1], args[2]);
    if (args.size() == 3 && args[0] == "sign")
        return signFolder(args[1], args[2]);
    if (args.size() == 2 && args[0] == "check")
        return check(args[1]);
    return usage();
}
