/* MSHV update check (macOS port, 2026-10-08) -- is there a newer release of
 * this port on GitHub?  No Sparkle, no server of our own, nothing downloaded
 * or installed: one GET of the GitHub API and, if there is something newer,
 * a dialog whose Download button opens the release page in the browser.
 *
 *   GET https://api.github.com/repos/vu2cpl/mshv-macos-port/releases/latest
 *       Accept: application/vnd.github+json
 *       User-Agent: MSHV-macOS/<this version>         no token, 10 s timeout
 *
 * Release tags look like v2.76.7-mac12 and compare as the numbers
 * (2,76,7,12) against VERSION_MAJOR/MINOR/PATCH + MSHV_MAC_REV (config.h).
 *
 * - Automatic: ~10 s after start, and again from an hourly timer while MSHV
 *   runs (it stays open for days), while "Check for Updates Automatically"
 *   is on (the default).  It runs only if the last SUCCESSFUL check is 24 h
 *   old, no failed automatic attempt in the last hour, and no update window
 *   is showing; MshvAutoCheckDecision() is that rule, as a pure function.
 *   Success = HTTP 200 and a reply whose tag_name is a version, newer or
 *   not; only then is LastCheck written.  A failed automatic check (offline,
 *   timeout, any HTTP error incl. 403, a bad reply) writes nothing -- the
 *   next start tries again -- and, in memory only, holds the next automatic
 *   attempt off for an hour.  Silent on any failure, on "up to date", and
 *   for a version the operator skipped.  (Manoj, 2026-10-09.)
 * - Development builds: if the version compared contains "dev" (any case),
 *   no automatic check at all; "Check for Updates..." still works.  MSHV's
 *   own versions never contain it -- the rule is common to Manoj's apps.
 * - Manual: "Check for Updates..." reports a newer version, "up to date", or
 *   why the check failed.
 * - Newer: release notes (plain text) + Download / Skip This Version /
 *   Remind Me Later.  The dialog is not modal; the request never blocks.
 *   One update window at a time: a new dialog or message box replaces the
 *   one showing, and the automatic check waits while one is open.
 * - Keyboard (Manoj, 2026-10-09, "like JTDX-VU"): no button in an update
 *   window is the default, so Return never opens the browser; Esc = Remind
 *   Me Later.  The automatic dialog opens without taking the keyboard
 *   (WA_ShowWithoutActivating, not raised or activated), so the main
 *   window keeps its focused widget; a manual check comes to the front.
 *
 * Both actions are added to the Help menu with ApplicationSpecificRole, so
 * on a Mac Cocoa shows them in the application menu, right under "About
 * MSHV" (which Qt moves there itself).
 *
 * State lives in <data dir>/settings/ms_update.ini (QSettings, [Update]
 * AutoCheck / LastCheck / SkippedVersion) -- NEVER in ms_settings, whose
 * reader takes keys in st_id order and must not grow (CLAUDE.md Rule 1).
 *
 * Test hook, inert unless set: MSHV_UPDATE_TEST_VERSION=2.76.6-mac1 makes
 * this build compare (and show itself) as that version, and lets the
 * automatic check ignore the 24 h gate, so a test launch sees the dialog
 * against the real latest release.  Being the version compared, it is also
 * what the "dev" rule looks at (a value with "dev" in it shows the skip).
 *
 * Mac-only: listed in MSHV_macOS.pro only, created by Main_Ms under
 * #if defined _MACOS_.  Plain Qt 5 + C++11 (Rule 7: builds on Qt 5.6.3).
 */
#ifndef MSHV_UPDATE_CHECK_H
#define MSHV_UPDATE_CHECK_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QPointer>
#include <QDateTime>
#include <QElapsedTimer>

class QAction;
class QMenu;
class QWidget;
class QDialog;
class QTimer;
class QNetworkAccessManager;
class QNetworkReply;

// One release, as read from the GitHub API.
struct MshvReleaseInfo
{
    QString tag;        // "v2.76.7-mac13"
    QString name;
    QString html_url;   // the release page
    QString body;       // release notes (Markdown, shown as plain text)
};

// ---- Pure helpers: no network, no UI (unit-tested on their own) ----
// "v2.76.7-mac12", "2.76.7-mac12-priv", "2.76.7 mac12" -> {2,76,7,12}.  A
// version without a mac part gets mac 0.  False unless it starts with
// major.minor.patch.
bool MshvParseVersionTag(const QString &tag, int v[4]);
// -1 / 0 / 1 for a < b / a == b / a > b, comparing (major, minor, patch, mac).
int MshvCompareVersions(const int a[4], const int b[4]);
// True only if both parse and latest is greater than current.
bool MshvIsNewerVersion(const QString &latest, const QString &current);
// {2,76,7,12} -> "2.76.7 mac12", the window title's form.
QString MshvVersionDisplay(const int v[4]);
// This build, from config.h: "2.76.7-mac12".
QString MshvBuildVersion();
// The /releases/latest reply.  False, with a reason in err, unless it is a
// JSON object whose tag_name parses as a version.
bool MshvParseReleaseJson(const QByteArray &json, MshvReleaseInfo &out, QString &err);
// A development build: the version contains "dev", in any case.
bool MshvIsDevVersion(const QString &version);

// Everything the automatic check depends on, so that whether it runs is a
// pure function of these (MshvAutoCheckDecision).
struct MshvAutoCheckInputs
{
    bool auto_on;           // "Check for Updates Automatically" ticked
    QString current;        // the version compared: this build, or the test hook
    bool test_mode;         // MSHV_UPDATE_TEST_VERSION set: no 24 h gate
    bool busy;              // a request is already running
    bool window_open;       // the update dialog or a check's message box is showing
    QDateTime last_ok;      // LastCheck = the last successful check (invalid: never)
    QDateTime now;          // UTC
    qint64 since_fail_ms;   // since the last failed automatic attempt, -1 = none
};
enum MshvAutoCheckVerdict
{
    MshvAutoDue = 0,        // check now
    MshvAutoOff,            // the operator turned it off
    MshvAutoDevBuild,       // the version compared contains "dev"
    MshvAutoBusy,           // a request is running
    MshvAutoWindowOpen,     // never two update windows
    MshvAutoBackoff,        // an automatic attempt failed less than 1 h ago
    MshvAutoRecent          // the last successful check is less than 24 h old
};
MshvAutoCheckVerdict MshvAutoCheckDecision(const MshvAutoCheckInputs &in);

// ---- Network half: one GET of releases/latest at a time ----
class MshvReleaseFetcher : public QObject
{
    Q_OBJECT
public:
    enum ErrKind { ErrNone = 0, ErrNetwork, ErrTimeout, ErrHttp, ErrReply };
    explicit MshvReleaseFetcher(QObject *parent = 0);
    void SetUrl(const QString &u) { url = u; }     // tests only; default: the port's public repo
    bool Busy() const { return !reply.isNull(); }
    void Start();
    const MshvReleaseInfo &Info() const { return info; }
    ErrKind Error() const { return err_kind; }
    QString ErrorDetail() const { return err_detail; }

signals:
    void Done(bool ok);

private slots:
    void Finished();
    void TimedOut();

private:
    QNetworkAccessManager *nam;
    QPointer<QNetworkReply> reply;
    QTimer *timeout;
    QString url;
    bool timed_out;
    MshvReleaseInfo info;
    ErrKind err_kind;
    QString err_detail;
};

// ---- UI half: menu actions, automatic check, dialogs, ms_update.ini ----
class MshvUpdateCheck : public QObject
{
    Q_OBJECT
public:
    // ini_path: <data dir>/settings/ms_update.ini; dlg_parent: the main window
    // (also this object's parent).  Arms the start-up and hourly checks.
    MshvUpdateCheck(const QString &ini_path, QWidget *dlg_parent);
    void AddToMenu(QMenu *m);

public slots:
    void CheckNow();

private slots:
    void AutoCheck();       // the start-up timer and the hourly one
    void AutoToggled(bool on);
    void FetchDone(bool ok);

private:
    void ShowNewer(const MshvReleaseInfo &r, const int latest[4], const int current[4], bool take_focus);
    void ShowMessage(bool failed, const QString &text);
    void ShowWindow(QDialog *w, bool take_focus);     // no default button; focus only if asked for
    QString DownloadUrl(const MshvReleaseInfo &r) const;

    QWidget *dlg_parent;
    QString ini_path;
    MshvReleaseFetcher *fetcher;
    QAction *a_check;
    QAction *a_auto;
    QTimer *recheck;        // hourly, re-armed when an automatic attempt ends
    QPointer<QDialog> dlg;  // the update window showing (dialog or message box), at most one
    QString current;        // this build, or MSHV_UPDATE_TEST_VERSION
    bool test_mode;
    bool manual;            // the running check was asked for by the operator
    bool auto_started;      // the running check was started automatically
    QElapsedTimer uptime;   // monotonic, from construction
    bool auto_failed;       // the last automatic attempt failed (in memory only)
    qint64 auto_fail_at;    // uptime.elapsed() at that failure
};

#endif
