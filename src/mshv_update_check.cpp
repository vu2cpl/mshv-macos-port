/* MSHV update check (macOS port) -- see mshv_update_check.h for what it
 * does, why it is shaped this way, and the test hook.
 */
#include "mshv_update_check.h"
#include "config.h"

#include <cstdio>
#include <QAction>
#include <QMenu>
#include <QWidget>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QPixmap>
#include <QFont>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <QSettings>
#include <QDateTime>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

static const char MSHV_UPDATE_API_URL[] =
    "https://api.github.com/repos/vu2cpl/mshv-macos-port/releases/latest";
static const int MSHV_UPDATE_TIMEOUT_MS = 10000;          // the request
static const int MSHV_UPDATE_START_DELAY_MS = 10000;      // after start-up
static const int MSHV_UPDATE_RECHECK_MS = 3600 * 1000;    // the hourly timer while running
static const qint64 MSHV_UPDATE_INTERVAL_S = 24 * 3600;   // since the last successful check
static const qint64 MSHV_UPDATE_RETRY_MS = 3600 * 1000;   // after a failed automatic attempt

// ------------------------------------------------------------------ pure --

bool MshvParseVersionTag(const QString &tag, int v[4])
{
    // optional v, major.minor.patch, then optionally [-_ .]mac<N>; anything
    // after that (e.g. "-priv") is ignored
    static const QRegularExpression rx(
        "^\\s*v?(\\d{1,6})\\.(\\d{1,6})\\.(\\d{1,6})(?:[-_ .]?mac(\\d{1,6}))?",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = rx.match(tag);
    if (!m.hasMatch()) return false;
    for (int i = 0; i < 3; ++i) v[i] = m.captured(i + 1).toInt();
    v[3] = m.captured(4).isEmpty() ? 0 : m.captured(4).toInt();
    return true;
}

int MshvCompareVersions(const int a[4], const int b[4])
{
    for (int i = 0; i < 4; ++i)
    {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

bool MshvIsNewerVersion(const QString &latest, const QString &current)
{
    int l[4], c[4];
    if (!MshvParseVersionTag(latest, l) || !MshvParseVersionTag(current, c)) return false;
    return MshvCompareVersions(l, c) > 0;
}

QString MshvVersionDisplay(const int v[4])
{
    QString s = QString("%1.%2.%3").arg(v[0]).arg(v[1]).arg(v[2]);
    if (v[3] > 0) s += QString(" mac%1").arg(v[3]);
    return s;
}

QString MshvBuildVersion()
{
    return QString("%1.%2.%3-mac%4").arg(VERSION_MAJOR).arg(VERSION_MINOR)
           .arg(VERSION_PATCH).arg(MSHV_MAC_REV);
}

bool MshvParseReleaseJson(const QByteArray &json, MshvReleaseInfo &out, QString &err)
{
    out = MshvReleaseInfo();
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !d.isObject())
    {
        err = "not a JSON object (" + pe.errorString() + ")";
        return false;
    }
    const QJsonObject o = d.object();
    out.tag      = o.value("tag_name").toString().trimmed();
    out.name     = o.value("name").toString().trimmed();
    out.html_url = o.value("html_url").toString().trimmed();
    out.body     = o.value("body").toString();
    out.body.replace("\r\n", "\n");
    int v[4];
    if (out.tag.isEmpty())
    {
        err = "no tag_name";
        return false;
    }
    if (!MshvParseVersionTag(out.tag, v))
    {
        err = "tag_name " + out.tag.left(40) + " is not a version";
        return false;
    }
    return true;
}

bool MshvIsDevVersion(const QString &version)
{
    return version.contains("dev", Qt::CaseInsensitive);
}

MshvAutoCheckVerdict MshvAutoCheckDecision(const MshvAutoCheckInputs &in)
{
    if (!in.auto_on) return MshvAutoOff;
    if (MshvIsDevVersion(in.current)) return MshvAutoDevBuild;
    if (in.busy) return MshvAutoBusy;
    if (in.window_open) return MshvAutoWindowOpen;
    if (in.since_fail_ms >= 0 && in.since_fail_ms < MSHV_UPDATE_RETRY_MS) return MshvAutoBackoff;
    if (!in.test_mode && in.last_ok.isValid() && in.now.isValid())
    {
        // a LastCheck in the future (the clock was set back) does not block
        const qint64 age = in.last_ok.secsTo(in.now);
        if (age >= 0 && age < MSHV_UPDATE_INTERVAL_S) return MshvAutoRecent;
    }
    return MshvAutoDue;
}

// --------------------------------------------------------------- network --

MshvReleaseFetcher::MshvReleaseFetcher(QObject *parent)
    : QObject(parent), nam(new QNetworkAccessManager(this)), timeout(new QTimer(this)),
      url(QString::fromLatin1(MSHV_UPDATE_API_URL)), timed_out(false), err_kind(ErrNone)
{
    timeout->setSingleShot(true);
    timeout->setInterval(MSHV_UPDATE_TIMEOUT_MS);
    connect(timeout, SIGNAL(timeout()), this, SLOT(TimedOut()));
}

void MshvReleaseFetcher::Start()
{
    if (Busy()) return;
    info = MshvReleaseInfo();
    err_kind = ErrNone;
    err_detail.clear();
    timed_out = false;

    QNetworkRequest req((QUrl(url)));
    req.setRawHeader("Accept", "application/vnd.github+json");
    // "MSHV-macOS/2.76.7-mac12" -- built from parts, so the binary carries no
    // literal matching the bundle check `strings | grep '^MSHV.*macOS'`,
    // which must find exactly one line (the version, config.h APP_NAME)
    req.setRawHeader("User-Agent", QString("MSHV-%1/%2").arg("macOS", MshvBuildVersion()).toLatin1());
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#else
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif
    // asynchronous: QNetworkAccessManager runs HTTP on its own thread, and the
    // answer comes back through finished() -- the UI never waits on it
    reply = nam->get(req);
    connect(reply.data(), SIGNAL(finished()), this, SLOT(Finished()));
    timeout->start();
}

void MshvReleaseFetcher::TimedOut()
{
    if (reply.isNull()) return;
    timed_out = true;
    reply->abort();     // emits finished(), handled below
}

void MshvReleaseFetcher::Finished()
{
    QNetworkReply *r = qobject_cast<QNetworkReply *>(sender());
    if (!r) return;
    if (r == reply.data()) reply.clear();
    timeout->stop();
    r->deleteLater();

    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = r->isOpen() ? r->read(4 * 1024 * 1024) : QByteArray();   // closed once aborted
    if (timed_out)
    {
        err_kind = ErrTimeout;
        emit Done(false);
        return;
    }
    if (status == 0)
    {
        err_kind = ErrNetwork;
        err_detail = r->errorString();
        emit Done(false);
        return;
    }
    if (status != 200)
    {
        err_kind = ErrHttp;
        err_detail = QString::number(status);
        // GitHub explains a refusal (rate limit, not found) in "message";
        // keep its first part ("API rate limit exceeded for <ip>."), not the
        // advertising that follows in brackets
        const QJsonDocument d = QJsonDocument::fromJson(data);
        QString msg = d.isObject() ? d.object().value("message").toString().simplified() : QString();
        const int br = msg.indexOf(" (");
        if (br > 0) msg = msg.left(br);
        if (msg.size() > 120) msg = msg.left(msg.lastIndexOf(' ', 120)) + "...";
        if (!msg.isEmpty()) err_detail += " - " + msg;
        emit Done(false);
        return;
    }
    if (!MshvParseReleaseJson(data, info, err_detail))
    {
        err_kind = ErrReply;
        emit Done(false);
        return;
    }
    emit Done(true);
}

// -------------------------------------------------------------------- UI --

MshvUpdateCheck::MshvUpdateCheck(const QString &ini, QWidget *parent)
    : QObject(parent), dlg_parent(parent), ini_path(ini), fetcher(new MshvReleaseFetcher(this)),
      a_check(0), a_auto(0), recheck(new QTimer(this)), test_mode(false), manual(false),
      auto_started(false), auto_failed(false), auto_fail_at(0)
{
    uptime.start();
    current = MshvBuildVersion();
    const QString tv = QString::fromLatin1(qgetenv("MSHV_UPDATE_TEST_VERSION")).trimmed();
    int v[4];
    if (!tv.isEmpty() && MshvParseVersionTag(tv, v))
    {
        current = tv;
        test_mode = true;
        fprintf(stderr, "[MSHV update] test hook: this build compares as %s, no 24 h gate\n",
                qPrintable(tv));
    }
    if (MshvIsDevVersion(current))
        fprintf(stderr, "[MSHV update] %s is a development version: no automatic checks\n",
                qPrintable(current));
    connect(fetcher, SIGNAL(Done(bool)), this, SLOT(FetchDone(bool)));

    QSettings s(ini_path, QSettings::IniFormat);
    a_check = new QAction(tr("Check for Updates..."), this);
    a_check->setMenuRole(QAction::ApplicationSpecificRole);     // beside About MSHV on a Mac
    connect(a_check, SIGNAL(triggered()), this, SLOT(CheckNow()));
    a_auto = new QAction(tr("Check for Updates Automatically"), this);
    a_auto->setCheckable(true);
    a_auto->setChecked(s.value("Update/AutoCheck", true).toBool());
    a_auto->setMenuRole(QAction::ApplicationSpecificRole);
    connect(a_auto, SIGNAL(toggled(bool)), this, SLOT(AutoToggled(bool)));

    QTimer::singleShot(MSHV_UPDATE_START_DELAY_MS, this, SLOT(AutoCheck()));
    // MSHV stays open for days: look again every hour; AutoCheck() decides.
    // Precise, so a tick re-armed when an attempt ends never comes early
    // (a coarse timer may fire up to 5 % early and miss the hour back-off).
    recheck->setTimerType(Qt::PreciseTimer);
    recheck->setInterval(MSHV_UPDATE_RECHECK_MS);
    connect(recheck, SIGNAL(timeout()), this, SLOT(AutoCheck()));
    recheck->start();
}

void MshvUpdateCheck::AddToMenu(QMenu *m)
{
    // Cocoa (Qt 5.15) inserts each ApplicationSpecificRole item directly under
    // About MSHV, so the one added LAST is shown FIRST: the toggle goes in
    // first to read "Check for Updates..." then "Check for Updates
    // Automatically" in the application menu (seen 2026-10-08 with the
    // opposite order)
    m->addAction(a_auto);
    m->addAction(a_check);
}

void MshvUpdateCheck::AutoToggled(bool on)
{
    QSettings s(ini_path, QSettings::IniFormat);
    s.setValue("Update/AutoCheck", on);
}

void MshvUpdateCheck::CheckNow()
{
    manual = true;      // a check already running now reports to the operator
    if (!fetcher->Busy()) fetcher->Start();
}

void MshvUpdateCheck::AutoCheck()
{
    MshvAutoCheckInputs in;
    in.auto_on = a_auto->isChecked();
    in.current = current;
    in.test_mode = test_mode;
    in.busy = fetcher->Busy();
    in.window_open = !dlg.isNull() && dlg->isVisible();
    {
        QSettings s(ini_path, QSettings::IniFormat);
        in.last_ok = QDateTime::fromString(s.value("Update/LastCheck").toString(), Qt::ISODate);
    }
    in.now = QDateTime::currentDateTimeUtc();
    in.since_fail_ms = auto_failed ? uptime.elapsed() - auto_fail_at : -1;
    if (MshvAutoCheckDecision(in) != MshvAutoDue) return;
    manual = false;
    auto_started = true;
    fetcher->Start();
}

void MshvUpdateCheck::FetchDone(bool ok)
{
    const bool was_manual = manual;
    const bool was_auto = auto_started;
    manual = false;
    auto_started = false;

    if (ok)
    {
        // success (HTTP 200, a version in tag_name, newer or not): the only
        // thing that writes LastCheck and so starts the next 24 h
        QSettings s(ini_path, QSettings::IniFormat);
        s.setValue("Update/LastCheck", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        auto_failed = false;
    }
    else if (was_auto)
    {
        // nothing written, so the next start tries again; in memory only, no
        // automatic retry for an hour.  A manual failure leaves this alone.
        auto_failed = true;
        auto_fail_at = uptime.elapsed();
    }
    // the next hourly look one full hour after this attempt, so a retry after
    // a failure, or the 24 h after a success, falls on a tick
    if (was_auto) recheck->start();

    if (!ok)
    {
        QString why;
        switch (fetcher->Error())
        {
        case MshvReleaseFetcher::ErrTimeout:
            why = tr("No answer from GitHub within 10 seconds.");
            break;
        case MshvReleaseFetcher::ErrHttp:
            why = tr("Reply from GitHub") + ": HTTP " + fetcher->ErrorDetail();
            break;
        case MshvReleaseFetcher::ErrReply:
            why = tr("The reply from GitHub has no usable release") + ": " + fetcher->ErrorDetail();
            break;
        default:
            why = tr("Network error") + ": " + fetcher->ErrorDetail();
            break;
        }
        fprintf(stderr, "[MSHV update] %s check failed: %s\n",
                was_manual ? "manual" : "automatic", qPrintable(why));
        if (was_manual) ShowMessage(true, tr("Could not check for updates.") + "\n\n" + why);
        return;
    }

    const MshvReleaseInfo &r = fetcher->Info();
    int lv[4] = {0, 0, 0, 0}, cv[4] = {0, 0, 0, 0};
    MshvParseVersionTag(r.tag, lv);          // the fetcher only accepts a parsable tag
    MshvParseVersionTag(current, cv);
    const bool newer = MshvCompareVersions(lv, cv) > 0;
    fprintf(stderr, "[MSHV update] %s check: latest release %s, this build %s -> %s\n",
            was_manual ? "manual" : "automatic", qPrintable(r.tag), qPrintable(current),
            newer ? "NEWER" : "up to date");

    if (!newer)
    {
        if (was_manual)
            ShowMessage(false, tr("You have the latest version of MSHV.") + "\n\n"
                        + tr("Installed version") + ": " + MshvVersionDisplay(cv) + "\n"
                        + tr("Latest release") + ": " + MshvVersionDisplay(lv));
        return;
    }
    if (!was_manual)
    {
        QSettings s(ini_path, QSettings::IniFormat);
        if (s.value("Update/SkippedVersion").toString() == r.tag) return;   // the operator said skip it
    }
    // a check the operator asked for (or joined by clicking) takes the focus;
    // one that started by itself does not
    ShowNewer(r, lv, cv, was_manual);
}

QString MshvUpdateCheck::DownloadUrl(const MshvReleaseInfo &r) const
{
    // the release's own page, if it really is a GitHub page
    const QUrl u(r.html_url);
    if (u.isValid() && u.scheme() == "https" && u.host() == "github.com") return r.html_url;
    return QString("https://github.com/vu2cpl/mshv-macos-port/releases/tag/")
           + QString::fromLatin1(QUrl::toPercentEncoding(r.tag));
}

// Return must never open the browser (Manoj, 2026-10-09: "like JTDX-VU"), so
// no push button in an update window is the default, not even by autoDefault.
// Run before AND after show(): QDialog makes its first autoDefault button the
// default when it is shown, and QMessageBox's button box makes its first
// accept button the default in its own show event.  Esc is untouched: it is
// QDialog::reject() -- Remind Me Later -- and a message box's only button.
static void MshvNoDefaultButton(QDialog *d)
{
    const QList<QPushButton *> bl = d->findChildren<QPushButton *>();
    for (int i = 0; i < bl.size(); ++i)
    {
        bl.at(i)->setAutoDefault(false);
        bl.at(i)->setDefault(false);
    }
}

void MshvUpdateCheck::ShowWindow(QDialog *w, bool take_focus)
{
    MshvNoDefaultButton(w);
    // An automatic check pops up on its own, maybe mid-QSO: leave the keyboard
    // where the operator is working.  Cocoa then orders the window front
    // without making it key, so the main window keeps its focused widget.
    if (!take_focus) w->setAttribute(Qt::WA_ShowWithoutActivating);
    dlg = w;                    // the hourly check stays quiet while it shows
    w->show();
    MshvNoDefaultButton(w);
    if (take_focus)             // asked for from the menu: to the front, as usual
    {
        w->raise();
        w->activateWindow();
    }
}

void MshvUpdateCheck::ShowMessage(bool failed, const QString &text)
{
    if (dlg) dlg->close();      // WA_DeleteOnClose: one update window at a time
    QMessageBox *mb = new QMessageBox(failed ? QMessageBox::Warning : QMessageBox::Information,
                                      tr("Check for Updates"), text, QMessageBox::Ok, dlg_parent);
    mb->setAttribute(Qt::WA_DeleteOnClose);
    mb->setModal(false);
    ShowWindow(mb, true);       // only a manual check reports up to date / a failure
}

void MshvUpdateCheck::ShowNewer(const MshvReleaseInfo &r, const int lv[4], const int cv[4], bool take_focus)
{
    if (dlg) dlg->close();      // WA_DeleteOnClose: one update window at a time

    QDialog *d = new QDialog(dlg_parent);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setModal(false);
    d->setWindowTitle(tr("Software Update"));

    QLabel *icon = new QLabel();
    icon->setPixmap(QPixmap(":pic/ms_ico.png").scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    icon->setAlignment(Qt::AlignTop);

    // Rule 6: whole sentences in tr(), the values outside them
    QLabel *head = new QLabel(tr("A new version of MSHV is available") + ": " + MshvVersionDisplay(lv));
    head->setTextFormat(Qt::PlainText);
    QFont fb = head->font();
    fb.setBold(true);
    head->setFont(fb);
    QLabel *have = new QLabel(tr("Installed version") + ": " + MshvVersionDisplay(cv));
    have->setTextFormat(Qt::PlainText);
    QLabel *notes_l = new QLabel(tr("Release notes") + ":");

    QPlainTextEdit *notes = new QPlainTextEdit();
    notes->setReadOnly(true);
    QString text = r.name;
    if (!r.body.trimmed().isEmpty()) text += (text.isEmpty() ? "" : "\n\n") + r.body.trimmed();
    notes->setPlainText(text);
    notes->setMinimumSize(500, 240);
    notes->setFocusPolicy(Qt::ClickFocus);

    // none of them is the default (ShowWindow): Return does nothing here
    QPushButton *b_skip = new QPushButton(tr("Skip This Version"));
    QPushButton *b_later = new QPushButton(tr("Remind Me Later"));
    QPushButton *b_dl = new QPushButton(tr("Download"));

    const QString url = DownloadUrl(r);
    const QString tag = r.tag;
    const QString ini = ini_path;
    connect(b_dl, &QPushButton::clicked, d, [d, url]() {
        fprintf(stderr, "[MSHV update] Download: %s\n", qPrintable(url));
        QDesktopServices::openUrl(QUrl(url));
        d->accept();
    });
    connect(b_skip, &QPushButton::clicked, d, [d, tag, ini]() {
        QSettings s(ini, QSettings::IniFormat);
        s.setValue("Update/SkippedVersion", tag);
        d->accept();
    });
    connect(b_later, &QPushButton::clicked, d, &QDialog::reject);

    QVBoxLayout *texts = new QVBoxLayout();
    texts->addWidget(head);
    texts->addWidget(have);
    texts->addSpacing(6);
    texts->addWidget(notes_l);
    texts->addWidget(notes);

    QHBoxLayout *top = new QHBoxLayout();
    top->addWidget(icon);
    top->setAlignment(icon, Qt::AlignTop);
    top->addLayout(texts, 1);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addWidget(b_skip);
    buttons->addStretch(1);
    buttons->addWidget(b_later);
    buttons->addWidget(b_dl);

    QVBoxLayout *all = new QVBoxLayout(d);
    all->addLayout(top, 1);
    all->addLayout(buttons);

    d->resize(600, 440);
    ShowWindow(d, take_focus);
}
