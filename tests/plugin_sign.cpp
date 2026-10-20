// The plugins' signatures (src/sign/pluginsignature.h): Ed25519 as RFC 8032
// has it, the manifest of a plugin's files, the same from its folder and
// from its archive, and what a file changed, added or signed with another
// key does to the signature.

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "check.h"
#include "core/zip.h"
#include "pluginsignature.h"
#include "testzip.h"

namespace {

QByteArray hex(const char* text)
{
    return QByteArray::fromHex(text);
}

bool write(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

}  // namespace

int main()
{
    // RFC 8032, 7.1, test 1: the empty message.
    {
        const auto keys = pluginsign::keysFromSeed(hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"));
        CHECK(keys.key == hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));
        const QByteArray signature = pluginsign::sign(QByteArray(), keys.secret);
        CHECK(signature == hex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
                               "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"));
        CHECK(pluginsign::verify({"RFC", keys.key, signature}, QByteArray()));
        CHECK(!pluginsign::verify({"RFC", keys.key, signature}, QByteArray("x")));
    }
    // New keys are new each time, and sign what they are given.
    const auto keys = pluginsign::newKeys(), other = pluginsign::newKeys();
    CHECK(keys.key.size() == 32 && keys.secret.size() == 64 && keys.key != other.key);
    CHECK(keys.secret.right(32) == keys.key);
    CHECK(pluginsign::fingerprint(keys.key).size() == 8 * 4 + 7);
    CHECK(pluginsign::sign("text", QByteArray(10, 'x')).isEmpty());

    QTemporaryDir folder;
    const QString plugin = folder.filePath("sample");
    CHECK(write(plugin + "/plugin.json", "{\"id\": \"sample\"}"));
    CHECK(write(plugin + "/linux-x86_64/libsample.so", QByteArray(5000, 'L')));
    CHECK(write(plugin + "/bin/tool", "#!"));
    const auto manifest = pluginsign::folderManifest(plugin);
    CHECK(manifest.has_value());
    if (!manifest)
        return checkSummary("plugin_sign");
    // The first line, then one line a file, sorted by path.
    const QList<QByteArray> lines = manifest->split('\n');
    CHECK(lines.size() == 5 && lines[0] == "TuxAPE plugin manifest 1" && lines[4].isEmpty());
    CHECK(lines[1].endsWith("  bin/tool") && lines[2].endsWith("  linux-x86_64/libsample.so") && lines[3].endsWith("  plugin.json"));
    CHECK(lines[1].indexOf("  ") == 128);

    // Signed: signature.json beside the files, not among what is signed.
    pluginsign::Signature signature{"Someone", keys.key, pluginsign::sign(*manifest, keys.secret)};
    CHECK(pluginsign::verify(signature, *manifest));
    CHECK(write(plugin + "/signature.json", pluginsign::toJson(signature)));
    CHECK(pluginsign::folderManifest(plugin) == manifest);
    QFile json(plugin + "/signature.json");
    CHECK(json.open(QIODevice::ReadOnly));
    const auto read = pluginsign::parse(json.readAll());
    CHECK(read && read->signer == "Someone" && read->key == keys.key && read->signature == signature.signature);
    CHECK(!pluginsign::parse("{\"format\": 2}") && !pluginsign::parse("not json"));
    // Another key does not make that signature.
    CHECK(!pluginsign::verify({"Someone", other.key, signature.signature}, *manifest));

    // The same plugin in an archive, in a folder of its own or not: the
    // same manifest.
    const QString archive = folder.filePath("sample.zip"), flat = folder.filePath("flat.zip");
    CHECK(testzip::writeZip(archive, {{"sample/plugin.json", "{\"id\": \"sample\"}"},
                                      {"sample/linux-x86_64/libsample.so", QByteArray(5000, 'L')},
                                      {"sample/bin/tool", "#!"},
                                      {"sample/signature.json", pluginsign::toJson(signature)}}));
    CHECK(testzip::writeZip(flat, {{"bin/tool", "#!"}, {"plugin.json", "{\"id\": \"sample\"}"},
                                   {"linux-x86_64/libsample.so", QByteArray(5000, 'L')}}));
    const auto zip = tuxape::ZipArchive::open(archive.toStdString()), flatZip = tuxape::ZipArchive::open(flat.toStdString());
    CHECK(zip && flatZip);
    if (zip && flatZip) {
        CHECK(pluginsign::archiveManifest(*zip, "sample/") == manifest);
        CHECK(pluginsign::archiveManifest(*flatZip, "") == manifest);
    }

    // A byte changed, a file added: the signature is not theirs any more.
    CHECK(write(plugin + "/linux-x86_64/libsample.so", QByteArray(4999, 'L') + 'M'));
    CHECK(!pluginsign::verify(signature, *pluginsign::folderManifest(plugin)));
    CHECK(write(plugin + "/linux-x86_64/libsample.so", QByteArray(5000, 'L')));
    CHECK(pluginsign::verify(signature, *pluginsign::folderManifest(plugin)));
    CHECK(write(plugin + "/extra.so", "x"));
    CHECK(!pluginsign::verify(signature, *pluginsign::folderManifest(plugin)));
    // Digests of manifests, for approvals: the same for the same.
    CHECK(pluginsign::digest(*manifest) == pluginsign::digest(*manifest) && pluginsign::digest(*manifest).size() == 64);
    return checkSummary("plugin_sign");
}
