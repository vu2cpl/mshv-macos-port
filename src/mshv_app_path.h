/* MSHV data-directory resolver.
 *
 * Linux/Windows: data dirs (settings/, RxWavs/, log/, ...) sit next to the
 *   executable, so QCoreApplication::applicationDirPath() is the right
 *   answer.
 *
 * macOS: the executable lives at MSHV.app/Contents/MacOS/MSHV. User-mutable
 *   state lives in ~/Library/Application Support/MSHV/ so dragging a new
 *   .app to /Applications doesn't wipe settings/logs/QSO history. On first
 *   launch we seed the Library copy from the bundle's Contents/Resources/
 *   (cp -n semantics — never overwrite an existing file in Library).
 *
 *   We seed individual subdirs rather than cloning Resources wholesale
 *   because the bundle contains some read-only resources that don't need to
 *   live under Application Support (icns, qt.conf, lproj). Only the
 *   user-mutable subdirs make the trip.
 */
#ifndef MSHV_APP_PATH_H
#define MSHV_APP_PATH_H

#include <QCoreApplication>
#include <QString>
#include <QDir>
#include <QFile>

#if defined _MACOS_
static inline void mshv_copy_tree_if_missing(const QString &src, const QString &dst)
{
    QDir src_dir(src);
    if (!src_dir.exists()) return;
    QDir().mkpath(dst);
    QDir dst_dir(dst);

    const auto files = src_dir.entryList(
        QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QString &name : files)
    {
        const QString sf = src_dir.filePath(name);
        const QString df = dst_dir.filePath(name);
        if (!QFile::exists(df))
        {
            QFile::copy(sf, df);
            // QFile::copy preserves the source's read-only bit, which is
            // wrong for user-mutable state. Make it writable.
            QFile::setPermissions(df,
                QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                QFileDevice::ReadGroup | QFileDevice::ReadOther);
        }
    }
    const auto subdirs = src_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : subdirs)
        mshv_copy_tree_if_missing(src_dir.filePath(name), dst_dir.filePath(name));
}
#endif

static inline QString mshv_app_data_path()
{
#if defined _MACOS_
    const QString user_path = QDir::homePath() + "/Library/Application Support/MSHV";
    const QString bundle_resources = QDir::cleanPath(
        QCoreApplication::applicationDirPath() + "/../Resources");

    QDir().mkpath(user_path);

    // Subdirs MSHV writes to. Only present in the bundle as seed data; the
    // app reads/writes the Library copy at runtime. Seeding is cp -n style —
    // existing user data in Library is never overwritten.
    static const char *subdirs[] = {
        "settings", "log", "AllTxtMonthly", "ExportLog", "RxWavs", "Screenshots"
    };
    for (const char *sub : subdirs)
        mshv_copy_tree_if_missing(bundle_resources + "/" + sub,
                                   user_path        + "/" + sub);

    return user_path;
#else
    return QCoreApplication::applicationDirPath();
#endif
}

#endif
