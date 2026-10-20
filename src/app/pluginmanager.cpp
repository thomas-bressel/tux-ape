#include "pluginmanager.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QRegularExpression>
#include <QSysInfo>

#include "core/files.h"
#include "core/zip.h"
#include "pluginsignature.h"

namespace {

const char kManifest[] = "plugin.json";

// A path inside a plugin's folder, as its plugin.json or its archive gives
// it: one that stays inside.
bool staysInside(const QString& path)
{
    if (path.isEmpty() || path.startsWith('/') || path.contains('\\') || path.contains(':'))
        return false;
    const QStringList parts = path.split('/');
    return !parts.contains("..") && !parts.contains(".");
}

// The plugin an archive holds: its description, and the folder of the
// archive it is in ("" or "name/").
struct Archived {
    PluginInfo info;
    QString prefix;
};

std::optional<Archived> archived(const tuxape::ZipArchive& zip, QString* error)
{
    // plugin.json at the top, or in the one folder the archive is made of.
    const tuxape::ZipArchive::Entry* manifest = nullptr;
    for (const tuxape::ZipArchive::Entry& entry : zip.entries()) {
        const QString name = QString::fromStdString(entry.name);
        if (name != kManifest && !(name.count('/') == 1 && name.endsWith(QLatin1String("/") + kManifest)))
            continue;
        if (manifest) {
            if (error)
                *error = PluginManager::tr("the archive holds more than one plugin");
            return std::nullopt;
        }
        manifest = &entry;
    }
    const auto json = manifest ? zip.read(*manifest) : std::nullopt;
    if (!json) {
        if (error)
            *error = PluginManager::tr("the archive holds no plugin.json");
        return std::nullopt;
    }
    auto info = PluginManager::parse(QByteArray(reinterpret_cast<const char*>(json->data()),
                                                static_cast<qsizetype>(json->size())),
                                     error);
    if (!info)
        return std::nullopt;
    const QString name = QString::fromStdString(manifest->name);
    return Archived{*info, name.left(name.size() - static_cast<qsizetype>(sizeof kManifest) + 1)};
}

}  // namespace

// The key of TuxAPE's publisher: the plugins it signs are let in on every
// TuxAPE.
const char kPublisherKey[] = "IVzCnIqqc3Z9mmyJGOdNlIYTFercbfsRm6ore4K+sBU=";

PluginManager::PluginManager(SessionMaker maker, QObject* parent)
    : QObject(parent)
    , maker_(std::move(maker))
{
    scan();
}

PluginManager::~PluginManager()
{
    stopAll();
}

QString PluginManager::folder()
{
    // Said from outside, it is that folder.
    if (qEnvironmentVariableIsSet("TUXAPE_PLUGIN_DIR")) {
        const QString named = qEnvironmentVariable("TUXAPE_PLUGIN_DIR");
        return named.isEmpty() ? QString() : QDir::cleanPath(named);
    }
    const QString beside = QCoreApplication::applicationDirPath() + "/plugins";
    for (const QString& folder : {beside, QStringLiteral(TUXAPE_DEV_PLUGIN_DIR)})
        if (QDir(folder).exists())
            return QDir::cleanPath(folder);
    return QDir::cleanPath(beside);
}

QString PluginManager::platform()
{
    QString system = QSysInfo::kernelType();
    if (system == "winnt")
        system = "windows";
    else if (system == "darwin")
        system = "macos";
    // The processor the program was built for: its libraries must be for
    // the same.
    return system + '-' + QSysInfo::buildCpuArchitecture();
}

std::optional<PluginInfo> PluginManager::parse(const QByteArray& json, QString* error)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    const QJsonObject object = document.object();
    PluginInfo info;
    info.id = object.value("id").toString();
    static const QRegularExpression idShape("^[a-z0-9][a-z0-9_-]{0,31}$");
    if (!document.isObject() || !idShape.match(info.id).hasMatch()) {
        if (error)
            *error = tr("its plugin.json does not describe a plugin");
        return std::nullopt;
    }
    info.name = object.value("name").toString(info.id);
    info.version = object.value("version").toString();
    info.author = object.value("author").toString();
    info.licence = object.value("licence").toString();
    info.description = object.value("description").toString();
    info.api = object.value("api").toInt();
    const QJsonObject libraries = object.value("libraries").toObject();
    info.platforms = libraries.keys();
    info.library = libraries.value(platform()).toString();
    // A plugin made for an earlier version still finds all it was built
    // against; one made for a later version would ask for what is not here.
    if (info.api < tuxape::kOldestPluginApi || info.api > tuxape::kPluginApi)
        info.problem = tr("it is made for version %1 of TuxAPE's plugins, and this is version %2")
                           .arg(info.api)
                           .arg(tuxape::kPluginApi);
    else if (info.library.isEmpty())
        info.problem = tr("it has no library for this computer (%1)").arg(platform());
    else if (!staysInside(info.library))
        info.problem = tr("its library is not in its folder");
    return info;
}

std::optional<PluginInfo> PluginManager::inspect(const QString& archive, QString* error)
{
    const auto zip = tuxape::ZipArchive::open(archive.toStdString());
    if (!zip) {
        if (error)
            *error = tr("it is not a .zip archive");
        return std::nullopt;
    }
    const auto found = archived(*zip, error);
    return found ? std::optional<PluginInfo>(found->info) : std::nullopt;
}

const PluginInfo* PluginManager::plugin(const QString& id) const
{
    for (const PluginInfo& info : plugins_)
        if (info.id == id)
            return &info;
    return nullptr;
}

QObject* PluginManager::instance(const QString& id) const
{
    const auto found = loaded_.find(id);
    return found == loaded_.end() ? nullptr : found->second.loader->instance();
}

PluginInfo* PluginManager::find(const QString& id)
{
    return const_cast<PluginInfo*>(plugin(id));
}

void PluginManager::scan()
{
    plugins_.clear();
    const QString root = folder();
    if (root.isEmpty())
        return;
    for (const QFileInfo& entry : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        // A folder being unpacked is not a plugin yet.
        if (entry.fileName().startsWith('.'))
            continue;
        QFile manifest(entry.filePath() + '/' + kManifest);
        if (!manifest.open(QIODevice::ReadOnly))
            continue;
        QString error;
        PluginInfo info = parse(manifest.readAll(), &error).value_or(PluginInfo{});
        if (info.id.isEmpty()) {
            info.name = entry.fileName();
            info.problem = error;
        } else if (info.id != entry.fileName()) {
            info.problem = tr("its folder is not named after it (%1)").arg(info.id);
        } else if (info.problem.isEmpty() && !QFileInfo::exists(entry.filePath() + '/' + info.library)) {
            info.problem = tr("its library is missing (%1)").arg(info.library);
        }
        // The folder's name is what the plugin goes by here.
        info.id = entry.fileName();
        info.folder = entry.filePath();
        // Who made it, and whether it is let in.
        QFile signature(entry.filePath() + '/' + pluginsign::kSignatureFile);
        seal(info, pluginsign::folderManifest(info.folder),
             signature.open(QIODevice::ReadOnly) ? std::optional<QByteArray>(signature.readAll()) : std::nullopt);
        info.enabled = !disabled_.contains(info.id);
        info.loaded = loaded_.count(info.id) != 0;
        plugins_.push_back(info);
    }
    std::stable_sort(plugins_.begin(), plugins_.end(), [](const PluginInfo& a, const PluginInfo& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
}

QList<TrustedAuthor> PluginManager::builtInAuthors()
{
    return {{QStringLiteral("TuxAPE"), QByteArray::fromBase64(kPublisherKey)}};
}

bool PluginManager::trusts(const QByteArray& key) const
{
    for (const QList<TrustedAuthor>& list : {builtInAuthors(), authors_})
        for (const TrustedAuthor& author : list)
            if (author.key == key)
                return true;
    return false;
}

void PluginManager::seal(PluginInfo& info, const std::optional<QByteArray>& manifest,
                         const std::optional<QByteArray>& signature) const
{
    info.digest = manifest ? pluginsign::digest(*manifest) : QString();
    info.seal = PluginInfo::Seal::Unsigned;
    if (signature) {
        const auto read = pluginsign::parse(*signature);
        info.seal = PluginInfo::Seal::Broken;
        if (read) {
            info.signer = read->signer;
            info.signerKey = read->key;
            if (manifest && pluginsign::verify(*read, *manifest))
                info.seal = trusts(read->key) ? PluginInfo::Seal::Trusted : PluginInfo::Seal::Unknown;
        }
    }
    info.allowed = !info.digest.isEmpty() && allowed_.value(info.id) == info.digest;
    info.waitsForConsent = false;
    if (!info.problem.isEmpty())
        return;
    if (!manifest)
        info.problem = tr("its files cannot be read");
    else if (info.seal == PluginInfo::Seal::Broken)
        info.problem = tr("its files are not those its signature was made for: it was changed after it was signed");
    else if (info.seal != PluginInfo::Seal::Trusted && !info.allowed) {
        info.waitsForConsent = true;
        info.problem = info.seal == PluginInfo::Seal::Unknown
                           ? tr("it is signed by %1, an author you have not said to trust").arg(info.signer)
                           : tr("it is not signed, and you have not let it in");
    }
}

std::optional<PluginInfo> PluginManager::check(const QString& archive, QString* error) const
{
    const auto zip = tuxape::ZipArchive::open(archive.toStdString());
    if (!zip) {
        if (error)
            *error = tr("it is not a .zip archive");
        return std::nullopt;
    }
    auto found = archived(*zip, error);
    if (!found)
        return std::nullopt;
    PluginInfo info = found->info;
    std::optional<QByteArray> signature;
    if (const auto json = zip->read((found->prefix + pluginsign::kSignatureFile).toStdString()))
        signature = QByteArray(reinterpret_cast<const char*>(json->data()), static_cast<qsizetype>(json->size()));
    // An earlier problem stays the one said; the seal is had all the same.
    const QString problem = info.problem;
    info.problem.clear();
    seal(info, pluginsign::archiveManifest(*zip, found->prefix), signature);
    if (!problem.isEmpty()) {
        info.problem = problem;
        info.waitsForConsent = false;
    }
    return info;
}

void PluginManager::setTrust(const QStringList& authors, const QMap<QString, QString>& allowed)
{
    authors_.clear();
    for (const QString& line : authors) {
        const int space = line.indexOf(' ');
        const QByteArray key = QByteArray::fromBase64(line.left(space).toLatin1());
        if (space > 0 && key.size() == pluginsign::kKeySize)
            authors_.push_back({line.mid(space + 1).trimmed(), key});
    }
    allowed_ = allowed;
    scan();
    settle();
    emit changed();
}

QStringList PluginManager::trustedAuthors() const
{
    QStringList lines;
    for (const TrustedAuthor& author : authors_)
        lines << QString::fromLatin1(author.key.toBase64()) + ' ' + author.name;
    return lines;
}

void PluginManager::trustAuthor(const TrustedAuthor& author)
{
    if (author.key.size() != pluginsign::kKeySize || trusts(author.key))
        return;
    authors_.push_back(author);
    scan();
    settle();
    emit trustChanged();
    emit changed();
}

void PluginManager::allow(const QString& id, const QString& digest)
{
    if (id.isEmpty() || digest.isEmpty() || allowed_.value(id) == digest)
        return;
    allowed_[id] = digest;
    scan();
    settle();
    emit trustChanged();
    emit changed();
}

void PluginManager::allowAll()
{
    bool any = false;
    for (const PluginInfo& info : plugins_)
        if (info.waitsForConsent && !info.digest.isEmpty()) {
            allowed_[info.id] = info.digest;
            any = true;
        }
    if (!any)
        return;
    scan();
    settle();
    emit trustChanged();
    emit changed();
}

void PluginManager::setDisabled(const QStringList& ids)
{
    if (ids == disabled_)
        return;
    disabled_ = ids;
    for (PluginInfo& info : plugins_)
        info.enabled = !disabled_.contains(info.id);
    settle();
    emit changed();
}

void PluginManager::start()
{
    started_ = true;
    scan();
    settle();
    emit changed();
}

void PluginManager::stopAll()
{
    while (!loaded_.empty())
        unload(loaded_.begin()->first);
    started_ = false;
}

void PluginManager::settle()
{
    if (!started_ || !maker_)
        return;
    for (PluginInfo& info : plugins_) {
        if (info.loaded && !info.enabled)
            unload(info.id);
        else if (!info.loaded && info.enabled && info.problem.isEmpty())
            load(info);
    }
}

void PluginManager::load(PluginInfo& info)
{
    auto loader = std::make_unique<QPluginLoader>(info.folder + '/' + info.library);
    QObject* instance = loader->instance();
    if (!instance) {
        info.problem = tr("its library cannot be loaded: %1").arg(loader->errorString());
        return;
    }
    auto* plugin = qobject_cast<tuxape::Plugin*>(instance);
    if (!plugin) {
        loader->unload();
        info.problem = tr("its library is not a plugin for TuxAPE");
        return;
    }
    std::unique_ptr<PluginSession> session = maker_(info.id);
    QString why;
    if (!plugin->start(session.get(), &why)) {
        session.reset();
        loader->unload();
        info.problem = why.isEmpty() ? tr("it could not start") : why;
        return;
    }
    info.loaded = true;
    loaded_[info.id] = Loaded{std::move(loader), plugin, std::move(session)};
}

void PluginManager::unload(const QString& id)
{
    const auto found = loaded_.find(id);
    if (found == loaded_.end())
        return;
    found->second.plugin->stop();
    // What the plugin added to the window goes first, then the windows it
    // asked to have deleted: nothing of the library's may be left to run.
    found->second.session.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    found->second.loader->unload();
    loaded_.erase(found);
    if (PluginInfo* info = find(id))
        info->loaded = false;
}

bool PluginManager::install(const QString& archive, QString* error)
{
    const auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return false;
    };
    const auto zip = tuxape::ZipArchive::open(archive.toStdString());
    if (!zip)
        return fail(tr("it is not a .zip archive"));
    QString why;
    const auto found = archived(*zip, &why);
    if (!found)
        return fail(why);
    const PluginInfo& info = found->info;
    if (!info.problem.isEmpty())
        return fail(info.problem);

    const QString root = folder();
    if (root.isEmpty() || !QDir().mkpath(root))
        return fail(tr("there is no plugins folder to put it in"));
    // Unpacked beside its place, then moved there: a plugin is whole or is
    // not there.
    const QString unpacked = root + "/." + info.id + ".new";
    QDir(unpacked).removeRecursively();
    for (const tuxape::ZipArchive::Entry& entry : zip->entries()) {
        const QString name = QString::fromStdString(entry.name);
        if (!name.startsWith(found->prefix))
            continue;
        const QString path = name.mid(found->prefix.size());
        const auto data = staysInside(path) ? zip->read(entry) : std::nullopt;
        const QString file = unpacked + '/' + path;
        if (!data || !QDir().mkpath(QFileInfo(file).absolutePath()) || !tuxape::writeFile(file.toStdString(), *data)) {
            QDir(unpacked).removeRecursively();
            return fail(tr("%1 cannot be unpacked").arg(name));
        }
        // Programs a plugin brings with it are to be run.
        if (path.startsWith("bin/"))
            QFile::setPermissions(file, QFile::permissions(file) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
    }
    if (!QFileInfo::exists(unpacked + '/' + info.library)) {
        QDir(unpacked).removeRecursively();
        return fail(tr("its library is missing (%1)").arg(info.library));
    }

    const QString place = root + '/' + info.id;
    unload(info.id);
    if ((QDir(place).exists() && !QDir(place).removeRecursively()) || !QDir().rename(unpacked, place)) {
        QDir(unpacked).removeRecursively();
        scan();
        emit changed();
        return fail(tr("%1 cannot be written to").arg(QDir::toNativeSeparators(place)));
    }
    scan();
    settle();
    emit changed();
    return true;
}

bool PluginManager::remove(const QString& id, QString* error)
{
    const PluginInfo* info = plugin(id);
    if (!info || info->folder.isEmpty())
        return false;
    const QString place = info->folder;
    unload(id);
    const bool gone = QDir(place).removeRecursively();
    // Gone, it is not let in any more.
    if (gone && allowed_.remove(id))
        emit trustChanged();
    if (!gone && error)
        *error = tr("%1 cannot be deleted").arg(QDir::toNativeSeparators(place));
    scan();
    emit changed();
    return gone;
}
