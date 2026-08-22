#include "main_window.h"
#include "job_options.h"
#include "job_widget.h"
#include "list_of_job_options.h"
#include "preferences_dialog.h"
#include "remote_widget.h"
#include "transfer_dialog.h"
#include "utils.h"
#ifdef Q_OS_MACOS
#include "global.h"
#include "mac_os_notifications.h"
#include "mac_os_power_saving.h"
#include "osx_helper.h"
#endif
#include "file_dialog.h"

namespace {
// ARMGDDN Browser: remote-folder grouping.
//
// Folders are defined in the ini under a [RemoteFolders] group as
//   FolderName=pattern
// where pattern uses '*' as a wildcard, e.g.
//   Titles=TO-*     (remotes whose name starts with "TO-")
//   HD=*hd*         (remotes whose name contains "hd")
//   4K=*4k          (remotes whose name ends with "4k")
// Matching remotes are shown grouped under a collapsible folder header. This
// only changes how the browser displays remotes - the rclone config is never
// touched.
constexpr int kItemKindRole = Qt::UserRole + 1;   // "folder" for headers
constexpr int kFolderNameRole = Qt::UserRole + 2; // owning folder (members)
constexpr int kCollapsedRole = Qt::UserRole + 3;  // bool (folder headers)
constexpr int kFolderCountRole = Qt::UserRole + 4; // member count (headers)

bool isFolderHeader(const QListWidgetItem *item) {
  return item && item->data(kItemKindRole).toString() == "folder";
}

// ARMGDDN Browser: desaturate an icon (keeping alpha) so remotes shown inside a
// mirror folder read as black-and-white, making the folder boundary obvious.
QIcon grayscaleIcon(const QIcon &icon) {
  const QList<QSize> sizes = icon.availableSizes();
  int side = 256;
  if (!sizes.isEmpty()) {
    side = qMax(sizes.first().width(), sizes.first().height());
  }
  QPixmap pm = icon.pixmap(QSize(side, side));
  QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < img.height(); ++y) {
    QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
    for (int x = 0; x < img.width(); ++x) {
      const QRgb p = line[x];
      const int g = qGray(p);
      line[x] = qRgba(g, g, g, qAlpha(p));
    }
  }
  return QIcon(QPixmap::fromImage(img));
}

void updateFolderHeaderText(QListWidgetItem *header) {
  QString name = header->data(Qt::UserRole).toString();
  int count = header->data(kFolderCountRole).toInt();
  bool collapsed = header->data(kCollapsedRole).toBool();
  header->setText(
      QString("%1  %2  (%3)")
          .arg(collapsed ? QStringLiteral("▸") : QStringLiteral("▾"))
          .arg(name)
          .arg(count));
}

// Build a case-insensitive matcher from a single term. A term with '*' is a
// wildcard matched against the whole name; a plain term matches names that
// contain it.
QRegularExpression termToRegex(const QString &term) {
  if (term.contains('*')) {
    return QRegularExpression(
        QRegularExpression::wildcardToRegularExpression(term),
        QRegularExpression::CaseInsensitiveOption);
  }
  return QRegularExpression(QRegularExpression::escape(term),
                            QRegularExpression::CaseInsensitiveOption);
}

struct FolderRule {
  QString name;
  QRegularExpression include;
  QList<QRegularExpression> excludes;
};

void groupRemotesIntoFolders(QListWidget *remotes, const QString &img_add) {
  auto settings = GetSettings();
  settings->beginGroup("RemoteFolders");
  const QStringList folderNames = settings->childKeys();
  QList<FolderRule> folders;
  for (const QString &fname : folderNames) {
    QString pattern = settings->value(fname).toString().trimmed();
    if (pattern.isEmpty()) {
      continue;
    }
    // Syntax: include-pattern [ / exclude-term [ / exclude-term ... ] ]
    // Anything after a '/' is an exclude term, e.g.  Stuff-* / ftp  keeps the
    // Stuff-* remotes but drops any whose name contains "ftp".
    QStringList parts = pattern.split('/');
    FolderRule rule;
    rule.name = fname;
    rule.include = termToRegex(parts.takeFirst().trimmed());
    for (const QString &ex : parts) {
      QString t = ex.trimmed();
      if (!t.isEmpty()) {
        rule.excludes.append(termToRegex(t));
      }
    }
    folders.append(rule);
  }
  settings->endGroup();

  if (folders.isEmpty()) {
    return;
  }

  QIcon folderIcon(":media/images/qbutton_icons/ag_folder" + img_add + ".png");

  // snapshot the current flat list of remote items
  QList<QListWidgetItem *> allItems;
  while (remotes->count() > 0) {
    allItems.append(remotes->takeItem(0));
  }
  QVector<bool> claimed(allItems.size(), false);

  for (const auto &folder : folders) {
    QList<QListWidgetItem *> members;
    for (int i = 0; i < allItems.size(); ++i) {
      if (claimed[i]) {
        continue;
      }
      const QString name = allItems[i]->text();
      if (!folder.include.match(name).hasMatch()) {
        continue;
      }
      bool excluded = false;
      for (const QRegularExpression &ex : folder.excludes) {
        if (ex.match(name).hasMatch()) {
          excluded = true;
          break;
        }
      }
      if (excluded) {
        continue;
      }
      claimed[i] = true;
      members.append(allItems[i]);
    }
    if (members.isEmpty()) {
      continue;
    }

    QListWidgetItem *header = new QListWidgetItem(folderIcon, folder.name);
    header->setData(Qt::UserRole, folder.name);
    header->setData(kItemKindRole, "folder");
    header->setData(kCollapsedRole, false);
    header->setData(kFolderCountRole, members.size());
    QFont f = header->font();
    f.setBold(true);
    header->setFont(f);
    // subtle translucent tint so headers read as group dividers in both themes
    header->setBackground(QColor(128, 128, 128, 40));
    header->setToolTip("ARMGDDN mirror folder");
    // clickable (to expand/collapse) but not selectable/openable
    header->setFlags(Qt::ItemIsEnabled);
    updateFolderHeaderText(header);
    remotes->addItem(header);

    for (QListWidgetItem *m : members) {
      m->setData(kFolderNameRole, folder.name);
      // remotes inside a folder are shown in black-and-white so it is easy to
      // see where the folder's contents end and the ungrouped remotes begin
      m->setIcon(grayscaleIcon(m->icon()));
      remotes->addItem(m);
    }
  }

  // remotes that did not match any folder are listed as before
  for (int i = 0; i < allItems.size(); ++i) {
    if (!claimed[i]) {
      remotes->addItem(allItems[i]);
    }
  }
}
} // namespace

MainWindow::MainWindow() {

  ui.setupUi(this);

#ifdef Q_OS_MACOS
  // macOS power saving control object
  mMacOsPowerSaving = new MacOsPowerSaving();
#endif

  this->setWindowTitle("ARMGDDN Browser");

  auto settings = GetSettings();

#if defined(Q_OS_WIN) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  // disable "?" WindowContextHelpButton (Qt 6 no longer shows it by default)
  QApplication::setAttribute(Qt::AA_DisableWindowContextHelpButton);
#endif

#if !defined(Q_OS_MACOS)
  qApp->setStyle(QStyleFactory::create("Fusion"));

  bool darkMode = settings->value("Settings/darkMode").toBool();

  // enable dark mode for Windows and Linux
  if (darkMode) {

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText,
                         QColor(127, 127, 127));
    darkPalette.setColor(QPalette::Base, QColor(42, 42, 42));
    darkPalette.setColor(QPalette::AlternateBase, QColor(66, 66, 66));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text,
                         QColor(127, 127, 127));
    darkPalette.setColor(QPalette::Disabled, QPalette::Light,
                         QColor(35, 35, 35));
    darkPalette.setColor(QPalette::Dark, QColor(35, 35, 35));
    darkPalette.setColor(QPalette::Shadow, QColor(20, 20, 20));
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText,
                         QColor(127, 127, 127));
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Disabled, QPalette::Highlight,
                         QColor(80, 80, 80));
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);
    darkPalette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                         QColor(127, 127, 127));
    qApp->setPalette(darkPalette);
    qApp->setStyleSheet("QToolTip { color: #ffffff; background-color: #2a82da; "
                        "border: 1px solid white;}");
  }

#else

  QString sysInfo = QSysInfo::productVersion();
  // enable dark mode for older macOS
  if (sysInfo == "10.9" || sysInfo == "10.10" || sysInfo == "10.11" ||
      sysInfo == "10.12" || sysInfo == "10.13") {

    qApp->setStyle(QStyleFactory::create("Fusion"));

    auto settings = GetSettings();
    bool darkMode = settings->value("Settings/darkMode").toBool();
    if (darkMode) {

      QPalette darkPalette;
      darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
      darkPalette.setColor(QPalette::WindowText, Qt::white);
      darkPalette.setColor(QPalette::Disabled, QPalette::WindowText,
                           QColor(127, 127, 127));
      darkPalette.setColor(QPalette::Base, QColor(42, 42, 42));
      darkPalette.setColor(QPalette::AlternateBase, QColor(66, 66, 66));
      darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
      darkPalette.setColor(QPalette::ToolTipText, Qt::white);
      darkPalette.setColor(QPalette::Text, Qt::white);
      darkPalette.setColor(QPalette::Disabled, QPalette::Text,
                           QColor(127, 127, 127));
      darkPalette.setColor(QPalette::Disabled, QPalette::Light,
                           QColor(35, 35, 35));
      darkPalette.setColor(QPalette::Dark, QColor(35, 35, 35));
      darkPalette.setColor(QPalette::Shadow, QColor(20, 20, 20));
      darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
      darkPalette.setColor(QPalette::ButtonText, Qt::white);
      darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText,
                           QColor(127, 127, 127));
      darkPalette.setColor(QPalette::BrightText, Qt::red);
      darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
      darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
      darkPalette.setColor(QPalette::Disabled, QPalette::Highlight,
                           QColor(80, 80, 80));
      darkPalette.setColor(QPalette::HighlightedText, Qt::white);
      darkPalette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                           QColor(127, 127, 127));
      qApp->setPalette(darkPalette);
      qApp->setStyleSheet(
          "QToolTip { color: #ffffff; background-color: #2a82da; "
          "border: 1px solid white;}");
    }
  }
#endif

  mSystemTray.setIcon(qApp->windowIcon());
  if (settings->contains("MainWindow/geometry")) {
    restoreGeometry(settings->value("MainWindow/geometry").toByteArray());
  }
  // ARMGDDN Browser: optional "config check on start" - run ARMGDDNBrowser.cmd
  // (if it exists next to the app) and wait for it to finish BEFORE we resolve
  // the config, so a freshly-downloaded ag.conf is picked up.
  runConfigCheckOnStart();

  // ARMGDDN Browser: rclone binary and config are always taken from the
  // application folder (AG/rclone + ag.conf/rclone.conf). They are never
  // user-configurable.
  SetRclone(AutoDetectRclone());
  SetRcloneConf(AutoDetectRcloneConf());

  mAlwaysShowInTray =
      settings->value("Settings/alwaysShowInTray", false).toBool();
  mCloseToTray = settings->value("Settings/closeToTray", false).toBool();
  mNotifyFinishedTransfers =
      settings->value("Settings/notifyFinishedTransfers", true).toBool();
  mSoundNotif = settings->value("Settings/soundNotif", false).toBool();

  mSystemTray.setVisible(mAlwaysShowInTray);

  // during first run the lastUsed keys might not exist
  if (!(settings->contains("Settings/lastUsedSourceFolder"))) {
    // if lastUsedSourceFolder does not exist create new empty key
    settings->setValue("Settings/lastUsedSourceFolder", "");
  };
  if (!(settings->contains("Settings/lastUsedDestFolder"))) {
    // if lastUsedDestFolder does not exist create new empty key
    settings->setValue("Settings/lastUsedDestFolder", "");
  };
  if (!(settings->contains("Settings/defaultDownloadOptions"))) {
    // if defaultDownloadOptions does not exist create new empty key
    settings->setValue("Settings/defaultDownloadOptions", "");
  };
#ifdef Q_OS_MACOS
  // for macOS by default exclude .DS_Store files from uploads
  if (!(settings->contains("Settings/defaultUploadOptions"))) {
    // if defaultDownloadOptions does not exist create new empty key
    settings->setValue("Settings/defaultUploadOptions", "--exclude .DS_Store");
  };
#else
  if (!(settings->contains("Settings/defaultUploadOptions"))) {
    // if defaultDownloadOptions does not exist create new empty key
    settings->setValue("Settings/defaultUploadOptions", "");
  };
#endif
  if (!(settings->contains("Settings/defaultRcloneOptions"))) {
    // if defaultRcloneOptions does not exist create new empty key
    settings->setValue("Settings/defaultRcloneOptions", "--fast-list");
  };

  QString buttonStyle = settings->value("Settings/buttonStyle").toString();
  QString buttonSize = settings->value("Settings/buttonSize").toString();
  QString iconsColour = settings->value("Settings/iconsColour").toString();

  QString img_add = "";

  if (iconsColour == "white") {
    img_add = "_inv";
  }

  // ARMGDDN Browser: only the Remotes and Jobs toolbars remain.
  ui.actionRefresh->setIcon(
      QIcon(":media/images/qbutton_icons/refresh" + img_add + ".png"));
  ui.actionOpen->setIcon(
      QIcon(":media/images/qbutton_icons/open_remote" + img_add + ".png"));
  // Preferences button action is triggered via slot defined in ui file
  // as we dont want pref icon in the menu
  ui.buttonPrefs->setIcon(
      QIcon(":media/images/qbutton_icons/preferences" + img_add + ".png"));

  ui.actionStopAllTransfers->setIcon(
      QIcon(":media/images/qbutton_icons/stop" + img_add + ".png"));
  ui.actionCleanNotRunning->setIcon(
      QIcon(":media/images/qbutton_icons/purge" + img_add + ".png"));
  // triggered via slot so button text can be different than action menu
  ui.buttonSortByTime->setIcon(
      QIcon(":media/images/qbutton_icons/sortTimeZA" + img_add + ".png"));
  ui.buttonSortByStatus->setIcon(
      QIcon(":media/images/qbutton_icons/sortZA" + img_add + ".png"));

  QPixmap arrowDownPixmap(":media/images/qbutton_icons/arrowdown" + img_add +
                          ".png");
  QPixmap arrowUpPixmap(":media/images/qbutton_icons/arrowup" + img_add +
                        ".png");
  QPixmap mount1Pixmap(":media/images/qbutton_icons/mount1" + img_add + ".png");

  QPixmap sortZAPixmap(":media/images/qbutton_icons/sortZA" + img_add + ".png");
  QPixmap sortAZPixmap(":media/images/qbutton_icons/sortAZ" + img_add + ".png");
  QPixmap sortTimeZAPixmap(":media/images/qbutton_icons/sortTimeZA" + img_add +
                           ".png");
  QPixmap sortTimeAZPixmap(":media/images/qbutton_icons/sortTimeAZ" + img_add +
                           ".png");

  QIcon arrowDownIcon(arrowDownPixmap);
  QIcon arrowUpIcon(arrowUpPixmap);
  QIcon mount1Icon(mount1Pixmap);
  QIcon sortZAIcon(sortZAPixmap);
  QIcon sortAZIcon(sortAZPixmap);
  QIcon sortTimeZAIcon(sortTimeZAPixmap);
  QIcon sortTimeAZIcon(sortTimeAZPixmap);

  ui.refresh->setDefaultAction(ui.actionRefresh);
  ui.open->setDefaultAction(ui.actionOpen);
  ui.buttonStopAllJobs->setDefaultAction(ui.actionStopAllTransfers);
  ui.buttonCleanNotRunning->setDefaultAction(ui.actionCleanNotRunning);

  ui.buttonPrefs->setText("Prefs");

  // open remote should be not active when there is
  // no focus on any e.g. after start
  ui.open->setEnabled(false);

  // jobs buttons inactive after start
  ui.buttonStopAllJobs->setEnabled(false);
  ui.buttonCleanNotRunning->setEnabled(false);
  ui.buttonSortByTime->setEnabled(false);
  ui.buttonSortByStatus->setEnabled(false);

  int icon_w = 16;
  int icon_h = 16;
  if (buttonSize == "0") {
    icon_w = 24;
  }
  if (buttonSize == "1") {
    icon_w = 32;
  }
  if (buttonSize == "2") {
    icon_w = 48;
  }
  if (buttonSize == "3") {
    icon_w = 72;
  }
  if (buttonSize == "4") {
    icon_w = 96;
  }
  icon_h = icon_w;
  int button_width = 61;

  QList<QToolButton *> mainButtons{ui.refresh,           ui.open,
                                   ui.buttonPrefs,       ui.buttonStopAllJobs,
                                   ui.buttonCleanNotRunning, ui.buttonSortByTime,
                                   ui.buttonSortByStatus};
  for (QToolButton *b : mainButtons) {
    if (buttonStyle == "textandicon") {
      b->setIconSize(QSize(icon_w, icon_h));
      b->setMinimumWidth(button_width);
    } else if (buttonStyle == "textonly") {
      b->setToolButtonStyle(Qt::ToolButtonTextOnly);
      b->setMinimumWidth(button_width);
    } else {
      b->setToolButtonStyle(Qt::ToolButtonIconOnly);
      b->setIconSize(QSize(icon_w, icon_h));
    }
  }

  ui.buttonSortByStatus->setStyleSheet("QToolButton {border: 0;}");

  // statusTips
  ui.actionRefresh->setStatusTip("Refresh remotes view");
  ui.actionOpen->setStatusTip("Open remote");
  ui.preferences->setStatusTip("ARMGDDN Browser preferences (ALT-p)");
  ui.actionStopAllTransfers->setStatusTip("Stop all running transfer jobs");
  ui.actionCleanNotRunning->setStatusTip("Remove all not running jobs");

  QObject::connect(ui.preferences, &QAction::triggered, this, [=]() {
    PreferencesDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
      auto settings = GetSettings();
      // ARMGDDN Browser: only the download location is user-editable here;
      // rclone/config paths are auto-detected and other rclone options are
      // configured by editing the ini directly.
      settings->setValue("Settings/defaultDownloadDir",
                         dialog.getDefaultDownloadDir().trimmed());
      settings->setValue("Settings/defaultDownloadOptions",
                         dialog.getDefaultDownloadOptions().trimmed());
      settings->setValue("Settings/defaultRcloneOptions",
                         dialog.getDefaultRcloneOptions().trimmed());

      settings->setValue("Settings/checkRcloneBrowserUpdates",
                         dialog.getCheckRcloneBrowserUpdates());
      settings->setValue("Settings/checkRcloneUpdates",
                         dialog.getCheckRcloneUpdates());

      settings->setValue("Settings/alwaysShowInTray",
                         dialog.getAlwaysShowInTray());
      settings->setValue("Settings/closeToTray", dialog.getCloseToTray());
      settings->setValue("Settings/startMinimisedToTray",
                         dialog.getStartMinimisedToTray());

      settings->setValue("Settings/notifyFinishedTransfers",
                         dialog.getNotifyFinishedTransfers());
      settings->setValue("Settings/soundNotif", dialog.getSoundNotif());

      settings->setValue("Settings/showFolderIcons",
                         dialog.getShowFolderIcons());
      settings->setValue("Settings/showFileIcons", dialog.getShowFileIcons());
      settings->setValue("Settings/rowColors", dialog.getRowColors());
      settings->setValue("Settings/showHidden", dialog.getShowHidden());

      settings->setValue("Settings/darkMode", dialog.getDarkMode());
      settings->setValue("Settings/rememberLastOptions",
                         dialog.getRememberLastOptions());

      settings->setValue("Settings/buttonStyle",
                         dialog.getButtonStyle().trimmed());
      settings->setValue("Settings/iconsLayout",
                         dialog.getIconsLayout().trimmed());
      settings->setValue("Settings/iconsColour",
                         dialog.getIconsColour().trimmed());

      settings->setValue("Settings/fontSize", dialog.getFontSize().trimmed());
      settings->setValue("Settings/buttonSize",
                         dialog.getButtonSize().trimmed());
      settings->setValue("Settings/iconSize", dialog.getIconSize().trimmed());

      settings->setValue("Settings/useProxy", dialog.getUseProxy());
      settings->setValue("Settings/http_proxy",
                         dialog.getHttpProxy().trimmed());
      settings->setValue("Settings/https_proxy",
                         dialog.getHttpsProxy().trimmed());
      settings->setValue("Settings/no_proxy", dialog.getNoProxy().trimmed());

      settings->setValue("Settings/preemptiveLoading",
                         dialog.getPreemptiveLoading());
      settings->setValue("Settings/preemptiveLoadingLevel",
                         dialog.getPreemptiveLoadingLevel().trimmed());

      mFirstTime = true;
      rcloneGetVersion();

      mAlwaysShowInTray = dialog.getAlwaysShowInTray();
      mCloseToTray = dialog.getCloseToTray();
      mNotifyFinishedTransfers = dialog.getNotifyFinishedTransfers();
      mSoundNotif = dialog.getSoundNotif();

      mSystemTray.setVisible(mAlwaysShowInTray);
    }
  });

  // intercept tab closure
  MainWindow::connect(ui.tabs, SIGNAL(tabCloseRequested(int)), this,
                      SLOT(slotCloseTab(int)));

  QObject::connect(ui.quit, &QAction::triggered, this, [=]() {
    mCloseToTray = false;
    close();
  });

  QObject::connect(ui.about, &QAction::triggered, this, [=]() {
    QMessageBox::about(
        this, "ARMGDDN Browser",
        QString(
            R"(<h3>ARMGDDN Browser, v)" RCLONE_BROWSER_VERSION "</h3>"

            R"(<p>Forked from <a href="https://github.com/kapitainsky/RcloneBrowser">Rclone Browser</a> by kapitainsky (originally by Martins Mozeiko).</p>)"

            R"(<p>Edited with &#10084; by DMP of ARMGDDN Games.</p>)"));
  });
  QObject::connect(ui.aboutQt, &QAction::triggered, qApp,
                   &QApplication::aboutQt);

  // ARMGDDN Browser: Links menu
  QObject::connect(ui.linkBrowserDownload, &QAction::triggered, this, [=]() {
    QDesktopServices::openUrl(
        QUrl("https://github.com/DeliciousMeatPop/ARMGDDNBrowser"));
  });
  QObject::connect(ui.linkBetaSite, &QAction::triggered, this, [=]() {
    QDesktopServices::openUrl(QUrl("https://ARMGDDNBrowser.com"));
  });
  QObject::connect(ui.linkTelegram, &QAction::triggered, this, [=]() {
    QDesktopServices::openUrl(QUrl("https://t.me/ARMGDDNGames"));
  });
  QObject::connect(ui.linkDMP, &QAction::triggered, this, [=]() {
    QDesktopServices::openUrl(QUrl("https://t.me/SickSoThr33"));
  });
  QObject::connect(ui.linkOldMan, &QAction::triggered, this, [=]() {
    QDesktopServices::openUrl(QUrl("https://t.me/George_jefferson"));
  });

  QObject::connect(ui.remotes, &QListWidget::currentItemChanged, this, [=]() {
    if (ui.remotes->selectedItems().empty()) {
      ui.open->setEnabled(false);
    } else {
      ui.open->setEnabled(true);
    }
  });

  QObject::connect(ui.remotes, &QListWidget::itemSelectionChanged, this, [=]() {
    if (ui.remotes->selectedItems().empty()) {
      ui.open->setEnabled(false);
    } else {
      ui.open->setEnabled(true);
    }
  });

  QObject::connect(ui.remotes, &QListWidget::itemChanged, this, [=]() {
    if (ui.remotes->selectedItems().empty()) {
      ui.open->setEnabled(false);
    } else {
      ui.open->setEnabled(true);
    }
  });

  QObject::connect(ui.remotes, &QListWidget::itemClicked, this, [=]() {
    if (ui.remotes->selectedItems().empty()) {
      ui.open->setEnabled(false);
    } else {
      ui.open->setEnabled(true);
    }
  });

  QObject::connect(ui.remotes, &QListWidget::itemActivated, ui.open,
                   &QPushButton::clicked);

  // ARMGDDN Browser: clicking a remote-folder header expands/collapses it
  QObject::connect(
      ui.remotes, &QListWidget::itemClicked, this, [=](QListWidgetItem *item) {
        if (!isFolderHeader(item)) {
          return;
        }
        bool collapsed = !item->data(kCollapsedRole).toBool();
        item->setData(kCollapsedRole, collapsed);
        updateFolderHeaderText(item);
        QString folderName = item->data(Qt::UserRole).toString();
        for (int i = 0; i < ui.remotes->count(); ++i) {
          QListWidgetItem *it = ui.remotes->item(i);
          if (it->data(kFolderNameRole).toString() == folderName) {
            it->setHidden(collapsed);
          }
        }
      });

  QObject::connect(ui.refresh, &QPushButton::clicked, this,
                   &MainWindow::rcloneListRemotes);

  QObject::connect(ui.open, &QPushButton::clicked, this, [=]() {
    if (ui.remotes->selectedItems().size() != 0) {
      auto item = ui.remotes->selectedItems().front();
      QString type = item->data(Qt::UserRole).toString();
      QString name = item->text();
      QString remoteType = type;

      auto remote = new RemoteWidget(&mIcons, name, remoteType, ui.tabs);

      QObject::connect(remote, &RemoteWidget::addTransfer, this,
                       &MainWindow::addTransfer);

      QString nameTrimmed = name;

      if (name.length() > 15) {
        nameTrimmed = nameTrimmed.left(12) + "...";
      }

      int index = ui.tabs->addTab(remote, nameTrimmed);
      ui.tabs->setTabToolTip(index,
                             "type: " + remoteType + "\n\nname: " + name);
      ui.tabs->setCurrentIndex(index);
    }
  });

  QObject::connect(ui.tabs, &QTabWidget::tabCloseRequested, this,
                   [=](const int &index) {
                     // delete remote widget when tab closed
                     ui.tabs->widget(index)->deleteLater();
                   });

  QObject::connect(ui.tabs, &QTabWidget::tabCloseRequested, ui.tabs,
                   &QTabWidget::removeTab);

  QObject::connect(ui.actionCleanNotRunning, &QAction::triggered, this, [=]() {
    int jobsCount = ((ui.jobs->count() - 2) / 2 - mJobCount);

    int button = QMessageBox::question(this, "Rclone Browser",
                                       QString("There are %1 inactive job(s).\n"
                                               "\nDo you want to clean them?")
                                           .arg(jobsCount),
                                       QMessageBox::Yes | QMessageBox::No,
                                       QMessageBox::No);

    if (button == QMessageBox::Yes) {
      int widgetsCount = ui.jobs->count();
      for (int i = widgetsCount - 2; i >= 0; i = i - 2) {
        QWidget *widget = ui.jobs->itemAt(i)->widget();
        if (auto transfer = qobject_cast<JobWidget *>(widget)) {
          if (!(transfer->isRunning)) {
            emit transfer->closed();
          }
        }
      }
    }
  });

  //!!!  QObject::connect(ui.actionStopAllTransfers
  QObject::connect(ui.actionStopAllTransfers, &QAction::triggered, this, [=]() {
    mDoNotSort = true;

    // we only stop transfer jobs - others are intact
    if (mTransferJobCount != 0) {

      int button = QMessageBox::question(
          this, "Rclone Browser",
          QString("There are %1 transfer job(s) running.\n"
                  "\nDo you want to stop  them?")
              .arg(mTransferJobCount),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

      if (button == QMessageBox::Yes) {

        // safely terminate all running transfers
        int widgetsCount = ui.jobs->count();
        for (int i = widgetsCount - 2; i >= 0; i = i - 2) {
          QWidget *widget = ui.jobs->itemAt(i)->widget();
          if (auto transfer = qobject_cast<JobWidget *>(widget)) {
            if ((transfer->isRunning)) {
              emit transfer->cancel();
            }
          }
        }
      }
    }

    mDoNotSort = false;
    sortJobs();
  });

  QObject::connect(ui.actionSortByStatus, &QAction::triggered, this, [=]() {
    ui.buttonSortByTime->setStyleSheet("QToolButton {border: 0;}");
    ui.buttonSortByStatus->setStyleSheet("QToolButton {}");

    if (mJobsSort != "byDate") {
      mJobsStatusSortOrder = !mJobsStatusSortOrder;
    }

    mJobsSort = "byStatus";

    if (mJobsStatusSortOrder) {
      ui.buttonSortByStatus->setIcon(sortAZIcon);
    } else {
      ui.buttonSortByStatus->setIcon(sortZAIcon);
    }

    sortJobs();
  });

  //!!!  QObject::connect(ui.actionSortByTime
  QObject::connect(ui.actionSortByTime, &QAction::triggered, this, [=]() {
    ui.buttonSortByStatus->setStyleSheet("QToolButton {border: 0;}");
    ui.buttonSortByTime->setStyleSheet("QToolButton {}");

    // flip sort order
    if (mJobsSort != "byStatus") {
      mJobsTimeSortOrder = !mJobsTimeSortOrder;
    }

    mJobsSort = "byDate";

    if (mJobsTimeSortOrder) {
      ui.buttonSortByTime->setIcon(sortTimeAZIcon);
    } else {
      ui.buttonSortByTime->setIcon(sortTimeZAIcon);
    }

    sortJobs();
  });

  mUploadIcon = arrowUpIcon;
  mDownloadIcon = arrowDownIcon;
  mMountIcon = mount1Icon;

  // remove close button from the fixed Remotes and Jobs tabs
  ui.tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
  ui.tabs->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
  ui.tabs->tabBar()->setTabButton(1, QTabBar::RightSide, nullptr);
  ui.tabs->tabBar()->setTabButton(1, QTabBar::LeftSide, nullptr);

  ui.tabs->setCurrentIndex(0);

  QObject::connect(&mSystemTray, &QSystemTrayIcon::activated, this,
                   [=](QSystemTrayIcon::ActivationReason reason) {
                     if (reason == QSystemTrayIcon::DoubleClick ||
                         reason == QSystemTrayIcon::Trigger) {
                       showNormal();
                       mSystemTray.setVisible(mAlwaysShowInTray);
#ifdef Q_OS_MACOS
                       osxShowDockIcon();
#endif
                     }
                   });

  QObject::connect(&mSystemTray, &QSystemTrayIcon::messageClicked, this, [=]() {
    showNormal();
    mSystemTray.setVisible(mAlwaysShowInTray);
#ifdef Q_OS_MACOS
    osxShowDockIcon();
#endif

    ui.tabs->setCurrentIndex(1);
    if (mLastFinished) {
      mLastFinished->showDetails();
      ui.jobsArea->ensureWidgetVisible(mLastFinished);
    }
  });

  QMenu *trayMenu = new QMenu(this);
  QObject::connect(
      trayMenu->addAction("&Show"), &QAction::triggered, this, [=]() {
        MainWindow::setWindowState((windowState() & ~Qt::WindowMinimized) |
                                   Qt::WindowActive);
        MainWindow::show();  // bring window to top on macOS
        MainWindow::raise(); // bring window from minimized state on macOS
        MainWindow::activateWindow(); // bring window to front/unminimize on
                                      // windows
        mSystemTray.setVisible(mAlwaysShowInTray);
#ifdef Q_OS_MACOS
        osxShowDockIcon();
#endif
      });

  QObject::connect(trayMenu->addAction("&Quit"), &QAction::triggered, this,
                   [=]() {
                     if (canClose()) {
                       QApplication::quit();
                     }
                   });

  mSystemTray.setContextMenu(trayMenu);

  mStatusMessage = new QLabel();
  ui.statusBar->addWidget(mStatusMessage);
  ui.statusBar->setStyleSheet("QStatusBar::item { border: 0; }");

  QTimer::singleShot(0, ui.remotes, SLOT(setFocus()));

  // ARMGDDN Browser: the rclone/AG binary is auto-detected from the
  // application folder, so we can go straight to checking its version.
  rcloneGetVersion();

  // start minimised to tray
  if ((settings->value("Settings/startMinimisedToTray").toBool())) {
#ifdef Q_OS_MACOS
    osxHideDockIcon();
#endif
    mSystemTray.show();
    QTimer::singleShot(0, this, SLOT(hide()));
  }
}

MainWindow::~MainWindow() {
  auto settings = GetSettings();
  settings->setValue("MainWindow/geometry", saveGeometry());
}

QList<QListWidgetItem *>
MainWindow::sortListWidget(const QList<QListWidgetItem *> &list,
                           bool sortOrder) {

  QList<QListWidgetItem *> sortedList = list;

  int n = sortedList.size();
  int min_idx;

  // basic selection sort algorithm
  for (int i = 0; i < (n - 1); i++) {
    min_idx = i;
    for (int j = i + 1; j < n; j++) {

      JobOptionsListWidgetItem *item_j =
          static_cast<JobOptionsListWidgetItem *>(sortedList.at(j));
      JobOptions *jo_j = item_j->GetData();

      JobOptionsListWidgetItem *item_min_idx =
          static_cast<JobOptionsListWidgetItem *>(sortedList.at(min_idx));
      JobOptions *jo_min_idx = item_min_idx->GetData();

      if (sortOrder) {
        if ((jo_j->description).toUpper() <
            (jo_min_idx->description).toUpper()) {
          min_idx = j;
        }
      } else {
        if ((jo_j->description).toUpper() >
            (jo_min_idx->description).toUpper()) {
          min_idx = j;
        }
      }
    }

#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
    sortedList.swapItemsAt(min_idx, i);
#else
    sortedList.swap(min_idx, i);
#endif
  }
  return sortedList;
}

void MainWindow::quitApp(void) {
  // wait for all processes to stop
  if (mQuitInfoDelay == 3) {

    QMessageBox *msgBox = new QMessageBox(
        QMessageBox::Warning, "Quitting",
        "Terminating all processes\nbefore quitting, please wait.",
        QMessageBox::NoButton, this);
    msgBox->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint |
                           Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    msgBox->setStandardButtons(QMessageBox::NoButton);
    msgBox->setCursor(Qt::WaitCursor);
    msgBox->setAttribute(Qt::WA_DeleteOnClose);

    msgBox->show();

    mQuittingErrorMsgBox = msgBox;
  }

  bool processActive = false;
  bool unmountingFailed = false;
  int widgetsCount = ui.jobs->count();

  // loop over all jobs and clean them
  for (int i = widgetsCount - 2; i >= 0; i = i - 2) {
    QWidget *widget = ui.jobs->itemAt(i)->widget();
    if (auto transfer = qobject_cast<JobWidget *>(widget)) {
      if (transfer->isRunning) {
        processActive = true;
      } else {
        emit transfer->closed();
      }
    }
  };

  if (unmountingFailed) {
    // quitting failed
    // reset to 3 attempts again
    mQuitInfoDelay = 3;

    if (mQuittingErrorMsgBox != NULL) {
      mQuittingErrorMsgBox->hide();
      mQuittingErrorMsgBox = NULL;
    }

    QMessageBox::critical(
        this, "Unmounting failed",
        QString("Some mounts can't be unmounted. Make sure that they are "
                "not used by other programs. You can also try to unmount "
                "them directly from your OS."));

    mAppQuittingStatus = false;
    return;
  }

  if (processActive == false) {
    // no running widget - bye bye - quitting at last
    QApplication::quit();
  } else {
    // something still running we check again a bit later then
    QTimer::singleShot(200, Qt::CoarseTimer, this, SLOT(quitApp()));
    ++mQuitInfoDelay;
  }
}

void MainWindow::rcloneGetVersion() {
  bool firstTime = mFirstTime;
  mFirstTime = false;

  QProcess *p = new QProcess();

  QObject::connect(
      p,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [=](int code, QProcess::ExitStatus) {
        if (code == 0) {
          QString version = p->readAllStandardOutput().trimmed();

          // extract rclone version - numbers only
          QString rclone_info1 = version;
          QString rclone_version_no;
          int lineBreak = rclone_info1.indexOf('\n');
          if (lineBreak != -1) {
            rclone_info1.remove(lineBreak, rclone_info1.length() - lineBreak);
            rclone_version_no = rclone_info1;
            rclone_version_no.replace("rclone v", "");
            rclone_version_no.replace("-DEV", "");
          } else {
            // for very old rclone versions format was one line only
            rclone_version_no = rclone_info1.trimmed();
            rclone_version_no.replace("rclone v", "");
            rclone_version_no.replace("-DEV", "");
          }
          // save current version no in settings
          auto settings = GetSettings();
          settings->setValue("Settings/rcloneVersion", rclone_version_no);

#if defined(Q_OS_WIN32)
          // check if required version
          unsigned int result =
              compareVersion(rclone_version_no.toStdString(), "1.50");

          if (result == 2) {
            QMessageBox::warning(
                this, "",
                "For mount functionality to work you need "
                "rclone version at least v1.50 "
                "and your current version is v" +
                    rclone_version_no +
                    ". Mount will be disabled. \n\nPlease consider upgrading.");
          };
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 1)
          QStringList lines = version.split("\n", Qt::SkipEmptyParts);
#else
          QStringList lines = version.split("\n", QString::SkipEmptyParts);
#endif

          QString rclone_info2;
          QString rclone_info3;

          int counter = 0;
          foreach (QString line, lines) {
            line = line.trimmed();
            if (counter == 1)
              rclone_info2 = line.replace("- ", "");
            if (counter == 2)
              rclone_info3 = line.replace("- ", "");
            counter++;
          };

          QFileInfo appBundlePath;
#ifdef Q_OS_MACOS
          if (IsPortableMode()) {

            QFileInfo applicationPath = qApp->applicationFilePath();
            QFileInfo MacOSPath = applicationPath.dir().path();
            QFileInfo ContentsPath = MacOSPath.dir().path();
            appBundlePath = ContentsPath.dir().path();

            mStatusMessage->setText(rclone_info1 + ", " + rclone_info2 + ", " +
                                    rclone_info3);

            mStatusMessage->setToolTip(
                rclone_info1 + " in " +
                QDir::toNativeSeparators(GetRclone().replace(
                    appBundlePath.fileName() + "/Contents/MacOS/../../../",
                    "")) +
                ", " + rclone_info2 + ", " + rclone_info3);

          } else {

            mStatusMessage->setText(rclone_info1 + ", " + rclone_info2 + ", " +
                                    rclone_info3);

            mStatusMessage->setToolTip(
                rclone_info1 + " in " + QDir::toNativeSeparators(GetRclone()) +
                ", " + rclone_info2 + ", " + rclone_info3);
          }
#else
#ifdef Q_OS_WIN
          mStatusMessage->setText(rclone_info1 + ", " +
                                  rclone_info2 + ", " + rclone_info3);


          mStatusMessage->setToolTip(rclone_info1 + " in " +
                                  QDir::toNativeSeparators(GetRclone()) + ", " +
                                  rclone_info2 + ", " + rclone_info3);

#else
          if (IsPortableMode()) {
            QString xdg_config_home = qgetenv("XDG_CONFIG_HOME");
            QString appImageConfigFolder = xdg_config_home.right(xdg_config_home.length()-xdg_config_home.lastIndexOf("/"));

            mStatusMessage->setText(rclone_info1 + ", " +
                                  rclone_info2 + ", " + rclone_info3);

            mStatusMessage->setToolTip(rclone_info1 + " in " +
                                  QDir::toNativeSeparators(GetRclone().replace(appImageConfigFolder + "/..",  "")) + ", " +
                                  rclone_info2 + ", " + rclone_info3);


          } else {
            mStatusMessage->setText(rclone_info1 + ", " +
                                  rclone_info2 + ", " + rclone_info3);

            mStatusMessage->setToolTip(rclone_info1 + " in " +
                                  QDir::toNativeSeparators(GetRclone()) + ", " +
                                  rclone_info2 + ", " + rclone_info3);


         }
#endif
#endif

          rcloneListRemotes();
        } else {
          if (p->error() != QProcess::FailedToStart) {
            if (getConfigPassword(p)) {
              rcloneGetVersion();
            } else {
              close();
            }
            p->deleteLater();
            return;
          }

          if (firstTime) {
            if (p->error() == QProcess::FailedToStart) {
              QMessageBox::information(
                  this, "Error",
                  "AG/rclone was not found next to ARMGDDN Browser.\n\nPlease "
                  "make sure AG.exe (or rclone.exe) is in the same folder as "
                  "this application.");
            } else {
              QMessageBox::information(this, "Error",
                                       "Cannot check the AG/rclone version.\n\n"
                                       "Please make sure AG.exe (or rclone.exe) "
                                       "in this folder is valid.");
            }
          }
        }

        auto settings = GetSettings();

        /// check ARMGDDN Browser version

        // during first run the key might not exist yet
        if (!(settings->contains("Settings/checkRcloneBrowserUpdates"))) {
          settings->setValue("Settings/checkRcloneBrowserUpdates", true);
        };

        bool checkRcloneBrowserUpdates =
            settings->value("Settings/checkRcloneBrowserUpdates").toBool();

        // if check updates enabled in settings
        if (checkRcloneBrowserUpdates) {
          QString last_check;
          QString current_date = QDate::currentDate().toString();

          if (!(settings->contains("Settings/lastRcloneBrowserUpdateCheck"))) {
            settings->setValue("Settings/lastRcloneBrowserUpdateCheck",
                               current_date);
          } else { // read last check date
            last_check =
                settings->value("Settings/lastRcloneBrowserUpdateCheck")
                    .toString();
          };

          // dont check if already checked today (once per day only)
          if (!(last_check == current_date)) {
            settings->setValue("Settings/lastRcloneBrowserUpdateCheck",
                               current_date);

            // latest ARMGDDN Browser release
            QString url = "https://api.github.com/repos/DeliciousMeatPop/"
                          "ARMGDDNBrowser/releases/latest";
            QNetworkAccessManager manager;
            QNetworkReply *response = manager.get(QNetworkRequest(QUrl(url)));
            QEventLoop event;
            connect(response, SIGNAL(finished()), &event, SLOT(quit()));
            event.exec();
            QByteArray content = response->readAll();

            QJsonParseError jsonError;
            QJsonDocument document = QJsonDocument::fromJson(
                content, &jsonError); // parse and capture the error flag

            if (jsonError.error == QJsonParseError::NoError) {
              if (document.object().contains("tag_name")) {
                QJsonValue tag_name = document.object().value("tag_name");
                QString latest = tag_name.toString(QString());
                latest.replace("v", "");
                latest = latest.trimmed();

                // check if new version available and if yes display information
                unsigned int result =
                    compareVersion(latest.toStdString(), RCLONE_BROWSER_VERSION);
                // latest version is greater than current
                if (result == 1) {
                  QMessageBox::information(
                      this, "",
                      QString(
                          R"(<p>A new ARMGDDN Browser version is available</p>)"
                          R"(<p>You have: v)" RCLONE_BROWSER_VERSION "<br />"
                          R"(New version: v)" +
                          latest +
                          "</p>"
                          R"(<p>Visit <a href="https://github.com/DeliciousMeatPop/ARMGDDNBrowser/releases/latest">releases</a> to download</p>)"));
                };
              };
            };
          };
        };

        p->deleteLater();
      });

  QObject::connect(
      p, &QProcess::errorOccurred, this, [=](QProcess::ProcessError error) {
        QString errorString =
            QMetaEnum::fromType<QProcess::ProcessError>().valueToKey(error);

        QMessageBox::information(
            this, "Error",
            "Cannot start AG/rclone\n\n Error: " + errorString +
                "\n\nPlease make sure AG.exe (or rclone.exe) is in the same "
                "folder as ARMGDDN Browser.");
      });

  UseRclonePassword(p);
  p->start(GetRclone(),
           QStringList() << "version"
                         << "--ask-password=false",
           QIODevice::ReadOnly);
}

void MainWindow::runConfigCheckOnStart() {
  auto settings = GetSettings();
  if (!settings->value("Settings/checkRcloneUpdates", true).toBool()) {
    return;
  }
  QString configCmd = QDir(GetAppDir()).filePath("ARMGDDNBrowser.cmd");
  if (!QFileInfo(configCmd).exists()) {
    return;
  }
  QProcess updateProcess;
  updateProcess.setWorkingDirectory(GetAppDir());
#ifdef Q_OS_WIN
  updateProcess.setProgram("cmd.exe");
  updateProcess.setArguments(QStringList()
                             << "/c" << QDir::toNativeSeparators(configCmd));
#else
  // non-Windows dev convenience: run it through a shell
  updateProcess.setProgram("sh");
  updateProcess.setArguments(QStringList() << configCmd);
#endif
  updateProcess.start();
  // block until the config check completes (or 10 minutes elapse)
  updateProcess.waitForFinished(600000);
}

void MainWindow::rcloneListRemotes() {

  ui.remotes->clear();

  // ARMGDDN Browser: re-detect the binary and config on every refresh so that
  // if ag.conf/rclone.conf (or AG/rclone) appeared after start-up - e.g. a
  // config check downloaded it - hitting Refresh picks it up.
  SetRclone(AutoDetectRclone());
  SetRcloneConf(AutoDetectRcloneConf());

  auto settings = GetSettings();
  QString mIconsLayout = settings->value("Settings/iconsLayout").toString();

  QProcess *p = new QProcess();

  QObject::connect(
      p,
      static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
          &QProcess::finished),
      this, [=](int code, QProcess::ExitStatus) {
        if (code == 0) {
          QStyle *style = qApp->style();

          QString bytes = p->readAllStandardOutput().trimmed();
          QStringList items = bytes.split('\n');

          auto settings = GetSettings();
          bool darkModeIni = settings->value("Settings/darkModeIni").toBool();
          QString iconSize = settings->value("Settings/iconSize").toString();
          QString iconsColour =
              settings->value("Settings/iconsColour").toString();

          for (const QString &line : items) {
            if (line.isEmpty()) {
              continue;
            }

            QStringList parts = line.split(':');
            if (parts.count() != 2) {
              continue;
            }

            QString name = parts[0].trimmed();
            QString type = parts[1].trimmed();
            QString tooltip = "type: " + type + "\n\nname: " + name;

            QString img_add = "";
            int size;

            // medium scale by default
            double darkModeIconScale = 1.333;
            double lightModeiconScale = 2;
            // to avoid "variable not used" compiler error
            if (darkModeIconScale == lightModeiconScale) {
            };

            // set icons scale based on iconSize value
            if (iconSize == "S") {
              lightModeiconScale = 3;
              darkModeIconScale = 2;
            }

            if (iconSize == "M") {
              lightModeiconScale = 4;
              darkModeIconScale = 2.666;
            }

            if (iconSize == "L") {
              lightModeiconScale = 6;
              darkModeIconScale = 4;
            }

            if (iconSize == "XL") {
              lightModeiconScale = 8;
              darkModeIconScale = 5.333;
            }

            if (iconSize == "XXL") {
              lightModeiconScale = 15;
              darkModeIconScale = 10;
            }

            // disable scaling - all is fusion now
            // let's leave scaling logic for now
            darkModeIconScale = lightModeiconScale;

#if !defined(Q_OS_MACOS)
            // _inv only for dark mode
            // we use darkModeIni to apply mode active at startup
            if (darkModeIni) {
              if (iconsColour == "white") {
                img_add = "_inv";
              } else {
                img_add = "";
              }
            } else {
              img_add = "";
            }
#if defined(Q_OS_WIN)
            // on Windows dark theme changes PM_ListViewIconSize size
            // so we have to adjust
            if (darkModeIni) {
              size = darkModeIconScale *
                     style->pixelMetric(QStyle::PM_ListViewIconSize);
            } else {
              size = lightModeiconScale *
                     style->pixelMetric(QStyle::PM_ListViewIconSize);
            }
#else
             // for Linux/BSD PM_ListViewIconSize stays the same
             size = lightModeiconScale * style->pixelMetric(QStyle::PM_ListViewIconSize);
#endif
#else
             QString sysInfo = QSysInfo::productVersion();
             // dark mode on older macOS
             if (sysInfo == "10.9" ||
                 sysInfo == "10.10" ||
                 sysInfo == "10.11" ||
                 sysInfo == "10.12" ||
                 sysInfo == "10.13") {

               // on older macOS we also have to adjust icon size per mode
               if (darkModeIni) {
                 size = darkModeIconScale * style->pixelMetric(QStyle::PM_ListViewIconSize);
                 if (iconsColour == "white") {
                   img_add = "_inv";
                 } else {
                   img_add = "";
                 }
               } else {
                 size = lightModeiconScale * style->pixelMetric(QStyle::PM_ListViewIconSize);
                 img_add = "";
               }

             } else {
               // for macOS > 10.13 native dark mode does not change IconSize base
               size = 1.5 * lightModeiconScale * style->pixelMetric(QStyle::PM_ListViewIconSize);
               if (iconsColour == "white") {
                  img_add = "_inv";
               } else {
                   img_add = "";
               }
             }
#endif
            ui.remotes->setIconSize(QSize(size, size));

            if (mIconsLayout == "tiles") {
              ui.remotes->setViewMode(QListWidget::IconMode);
              // disable drag and drop
              ui.remotes->setMovement(QListView::Static);
              // always adjust icons after the window is resized
              ui.remotes->setResizeMode(QListView::Adjust);
              ui.remotes->setWrapping(true);
              ui.remotes->setGridSize(QSize(size + 20, size + 40));
              ui.remotes->setSpacing(10);
              ui.remotes->setTextElideMode(Qt::ElideMiddle);
            }
            if (mIconsLayout == "longlist") {
              ui.remotes->setViewMode(QListWidget::ListMode);
              ui.remotes->setResizeMode(QListView::Adjust);
              ui.remotes->setWrapping(false);
              ui.remotes->setGridSize(QSize(size + 800, size + 20));
            }
            if (mIconsLayout == "list") {
              ui.remotes->setViewMode(QListWidget::ListMode);
              ui.remotes->setResizeMode(QListView::Adjust);
              ui.remotes->setWrapping(true);
              ui.remotes->setGridSize(QSize(size + 100, size + 20));
              ui.remotes->setSpacing(10);
            }

            QString path = ":media/images/remotes_icons/" +
                           type.replace(' ', '_') + img_add + ".png";
            QIcon icon(QFile(path).exists()
                           ? path
                           : ":media/images/remotes_icons/unknown" + img_add +
                                 ".png");

            QListWidgetItem *item = new QListWidgetItem(icon, name);
            item->setData(Qt::UserRole, type);
            item->setToolTip(tooltip);
            ui.remotes->addItem(item);
          }

          // ARMGDDN Browser: group remotes into ini-defined folders
          QString folderImgAdd = (iconsColour == "white") ? "_inv" : "";
          groupRemotesIntoFolders(ui.remotes, folderImgAdd);
        } else {
          if (p->error() != QProcess::FailedToStart) {
            if (getConfigPassword(p)) {
              rcloneListRemotes();
            }
          }
        }
        p->deleteLater();
        ui.open->setEnabled(false);
      });

  QObject::connect(
      p, &QProcess::errorOccurred, this, [=](QProcess::ProcessError error) {
        QString errorString =
            QMetaEnum::fromType<QProcess::ProcessError>().valueToKey(error);

        QMessageBox::information(
            this, "Error",
            "Cannot start AG/rclone\n\n Error: " + errorString +
                "\n\nPlease make sure AG.exe (or rclone.exe) is in the same "
                "folder as ARMGDDN Browser.");
      });

  UseRclonePassword(p);
  p->start(GetRclone(),
           QStringList() << "listremotes" << GetRcloneConf()
                         << GetDefaultOptionsList("defaultRcloneOptions")
                         << "--long"
                         << "--ask-password=false",
           QIODevice::ReadOnly);
}

bool MainWindow::getConfigPassword(QProcess *p) {
  QString output = p->readAllStandardError().trimmed();
  if (output.indexOf("RCLONE_CONFIG_PASS") > 0) {
    bool ok;
    QString password = QInputDialog::getText(
        this, qApp->applicationDisplayName(),
        "Enter password for .rclone.conf configuration file:",
        QLineEdit::Password, QString(), &ok);
    if (ok) {
      SetRclonePassword(password);
      return true;
    }
  } else if (output.indexOf("unknown command \"listremotes\"") > 0) {
    QMessageBox::critical(this, qApp->applicationDisplayName(),
                          "It seems rclone version you are using is too "
                          "old.\nPlease upgrade to the latest version");
    return false;
  }
  return false;
}

bool MainWindow::canClose() {
  if (mJobCount == 0) {
    return true;
  }

  bool wasVisible = isVisible();

  ui.tabs->setCurrentIndex(1);
  showNormal();

  int button = QMessageBox::question(
      this, "Rclone Browser",
      QString("There are %1 job(s) running.\n"
              "\nDo you want to stop them and quit?")
          .arg(mJobCount),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (!wasVisible) {
    hide();
  }

  if (button == QMessageBox::Yes) {
    // make sure terminated job is not removed from the queue
    // we make close process aware that it is quitting
    mAppQuittingStatus = true;

    int widgetsCount = ui.jobs->count();
    for (int i = widgetsCount - 2; i >= 0; i = i - 2) {
      QWidget *widget = ui.jobs->itemAt(i)->widget();
      if (auto transfer = qobject_cast<JobWidget *>(widget)) {
        if (transfer->isRunning) {
          emit transfer->cancel();
        } else {
          emit transfer->closed();
        }
      }
    }
    return true;
  }
  return false;
}

void MainWindow::closeEvent(QCloseEvent *ev) {
  if (mCloseToTray && isVisible()) {
#ifdef Q_OS_MACOS
    osxHideDockIcon();
#endif
    mSystemTray.show();
    hide();
    ev->ignore();
    return;
  }

  if (canClose()) {
    // quitApp() will wait for everything to finish
    quitApp();
    ev->ignore();
  } else {
    ev->ignore();
  }
}

void MainWindow::offerMirrorRetry(const QString &source, const QString &dest,
                                  const QStringList &args) {
  // source looks like  remoteName:path/to/thing
  int colon = source.indexOf(':');
  if (colon < 0) {
    return;
  }
  QString remoteName = source.left(colon);
  QString path = source.mid(colon + 1);

  // all remote names currently listed (skip mirror-folder headers)
  QStringList allRemotes;
  for (int i = 0; i < ui.remotes->count(); ++i) {
    QListWidgetItem *it = ui.remotes->item(i);
    if (it->data(kItemKindRole).toString() == "folder") {
      continue;
    }
    allRemotes << it->text();
  }

  // find the mirror folder this remote belongs to and its other members
  QStringList siblings;
  auto settings = GetSettings();
  settings->beginGroup("RemoteFolders");
  const QStringList folderKeys = settings->childKeys();
  for (const QString &fkey : folderKeys) {
    QString pattern = settings->value(fkey).toString().trimmed();
    if (pattern.isEmpty()) {
      continue;
    }
    QStringList parts = pattern.split('/');
    QRegularExpression inc = termToRegex(parts.takeFirst().trimmed());
    QList<QRegularExpression> exc;
    for (const QString &e : parts) {
      QString t = e.trimmed();
      if (!t.isEmpty()) {
        exc.append(termToRegex(t));
      }
    }
    auto matches = [&](const QString &n) {
      if (!inc.match(n).hasMatch()) {
        return false;
      }
      for (const QRegularExpression &ex : exc) {
        if (ex.match(n).hasMatch()) {
          return false;
        }
      }
      return true;
    };
    if (matches(remoteName)) {
      for (const QString &n : allRemotes) {
        if (n != remoteName && matches(n)) {
          siblings << n;
        }
      }
      break; // first matching folder wins
    }
  }
  settings->endGroup();

  if (siblings.isEmpty()) {
    QMessageBox::warning(
        this, "Quota reached",
        "The mirror \"" + remoteName +
            "\" hit a download quota or rate limit.\n\nPlease try a different "
            "mirror.");
    return;
  }

  bool ok = false;
  QString chosen = QInputDialog::getItem(
      this, "Quota reached",
      "\"" + remoteName +
          "\" hit a download quota or rate limit.\n\nRetry the download from "
          "another mirror:",
      siblings, 0, false, &ok);
  if (!ok || chosen.isEmpty()) {
    return;
  }

  QString newSource = chosen + ":" + path;
  QStringList newArgs = args;
  for (int i = 0; i < newArgs.size(); ++i) {
    if (newArgs[i] == source) {
      newArgs[i] = newSource;
    }
  }

  addTransfer("Retry from mirror " + chosen + ": " + newSource, newSource, dest,
              newArgs, QUuid::createUuid().toString(), "",
              QUuid::createUuid().toString());
}

void MainWindow::addTransfer(const QString &message, const QString &source,
                             const QString &dest, const QStringList &args,
                             const QString &uniqueId,
                             const QString &transferMode,
                             const QString &requestId) {

  QProcess *transfer = new QProcess(this);
  transfer->setProcessChannelMode(QProcess::MergedChannels);

  auto widget = new JobWidget(transfer, message, args, source, dest, uniqueId,
                              transferMode, requestId);

  QObject::connect(widget, &JobWidget::quotaError, this,
                   &MainWindow::offerMirrorRetry, Qt::QueuedConnection);

  auto line = new QFrame();
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);

  //!!!  QObject::connect(  widget, &JobWidget::finished
  QObject::connect(
      widget, &JobWidget::finished, this,
      [=](const QString &info, const QString &jobFinalStatus) {
        QMutexLocker locker(&mMutex);

        if (mNotifyFinishedTransfers) {
          mLastFinished = widget;
#if defined(Q_OS_WIN)
          mSystemTray.showMessage(
              "Rclone Browser - transfer " + jobFinalStatus, info,
              QIcon(":media/images/program_icons/rclone-browser512.png"));
#else
#if defined(Q_OS_MACOS)
          MacOsNotification::Display(
              "Rclone Browser - transfer " + jobFinalStatus, info);
#else
          mSystemTray.showMessage("Rclone Browser - transfer " + jobFinalStatus,
                                  info, QSystemTrayIcon::Information);
#endif
#endif
        }

        if (mSoundNotif) {
          // play notification sound
#ifdef RB_HAVE_MULTIMEDIA
          if (mNotificationSound.source().isEmpty()) {
            mNotificationSound.setSource(
                QUrl("qrc:/media/sounds/notification-sound.wav"));
          }
          mNotificationSound.play();
#endif
        }

        --mTransferJobCount;

        if (mTransferJobCount == 0) {

      // allow entering sleep
#if defined(Q_OS_WIN)
          SetThreadExecutionState(ES_CONTINUOUS);
#endif
#if defined(Q_OS_MACOS)
          mMacOsPowerSaving->resumePowerSaving();
#endif
          // run custom script
          auto settings = GetSettings();
          QString transferOffScript =
              settings->value("Settings/transferOffScript").toString();

          bool jobLastFinishedScriptRun =
              settings->value("Settings/jobLastFinishedScriptRun", false)
                  .toBool();
          if (jobLastFinishedScriptRun) {

            if (!transferOffScript.isEmpty()) {
              runScript(transferOffScript);
            }
          }
        }

        if (--mJobCount == 0) {
          ui.tabs->setTabText(1, "Jobs");
        } else {
          ui.tabs->setTabText(1, QString("Jobs (%1)").arg(mJobCount));
        }

        ui.buttonStopAllJobs->setEnabled(mTransferJobCount != 0);
        ui.buttonCleanNotRunning->setEnabled(mJobCount !=
                                             (ui.jobs->count() - 2) / 2);

        // job status changed so we have to sort jobs list
        sortJobs();
      });

  QObject::connect(widget, &JobWidget::closed, this, [=]() {
    if (widget == mLastFinished) {
      mLastFinished = nullptr;
    }

    ui.jobs->removeWidget(widget);
    ui.jobs->removeWidget(line);
    widget->deleteLater();
    delete line;

    int _jobsCount = (ui.jobs->count() - 2) / 2;
    ui.buttonSortByTime->setEnabled(_jobsCount > 1);
    ui.buttonSortByStatus->setEnabled(_jobsCount > 1);

    //    sortJobs();

    if (ui.jobs->count() == 2) {
      ui.noJobsAvailable->show();
    }
    ui.buttonStopAllJobs->setEnabled(mTransferJobCount != 0);
    ui.buttonCleanNotRunning->setEnabled(mJobCount !=
                                         (ui.jobs->count() - 2) / 2);
  });

  if (ui.jobs->count() == 2) {
    ui.noJobsAvailable->hide();
  }

  ui.jobs->insertWidget(0, widget);
  ui.jobs->insertWidget(1, line);
  ++mTransferJobCount;

  int _jobsCount = (ui.jobs->count() - 2) / 2;
  ui.buttonSortByTime->setEnabled(_jobsCount > 1);
  ui.buttonSortByStatus->setEnabled(_jobsCount > 1);

  sortJobs();

  // prevent OS sleep when transfer running
#if defined(Q_OS_WIN)
  SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED |
                          ES_AWAYMODE_REQUIRED);
#endif
#if defined(Q_OS_MACOS)
  mMacOsPowerSaving->suspendPowerSaving();
#endif

  // run custom script
  auto settings = GetSettings();
  QString transferOnScript =
      settings->value("Settings/transferOnScript").toString();

  bool jobStartScriptRun =
      settings->value("Settings/jobStartScriptRun", false).toBool();

  if (jobStartScriptRun) {

    if (!transferOnScript.isEmpty()) {
      runScript(transferOnScript);
    }
  }

  ui.tabs->setTabText(1, QString("Jobs (%1)").arg(++mJobCount));

  ui.buttonStopAllJobs->setEnabled(mTransferJobCount != 0);
  ui.buttonCleanNotRunning->setEnabled(mJobCount != (ui.jobs->count() - 2) / 2);

  UseRclonePassword(transfer);
  transfer->start(GetRclone(), args + GetRcloneConf(), QIODevice::ReadOnly);

  ui.buttonStopAllJobs->setEnabled(mTransferJobCount != 0);
  ui.buttonCleanNotRunning->setEnabled(mJobCount != (ui.jobs->count() - 2) / 2);

  // job status changed so we have to sort jobs list
  sortJobs();
}

//  runs Script
void MainWindow::runScript(const QString &script) {

  QProcess *p = new QProcess();

  QObject::connect(p,
                   static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                       &QProcess::finished),
                   this, [=](int code, QProcess::ExitStatus) {
                     if (code == 0) {
                     }
                     p->deleteLater();
                   });

  QStringList scriptList;

  for (QString arg : script.split(QRegularExpression(" (?=[^\"]*(\"[^\"]*\"[^\"]*)*$)"))) {
    if (!arg.isEmpty()) {
      scriptList << arg.replace("\"", "");
    }
  }

  QString scriptCmd = scriptList.takeAt(0);
  QStringList scriptArgs = scriptList;

  p->start(scriptCmd, scriptArgs, QIODevice::ReadOnly);
}

void MainWindow::slotCloseTab(int index) {

  // only when last remote tab is closed return to remote list tab
  // only when closing current tab
  if (ui.tabs->currentIndex() == index) {

    if (ui.tabs->count() == 6) {
      if (index == 5) {
        ui.tabs->setCurrentIndex(0);
      }
    } else {

      if (ui.tabs->count() == 7) {
        ui.tabs->setCurrentIndex(5);
      }
    }
  }
}

void MainWindow::sortJobs() {

  //  bool mJobsTimeSortOrder = false;
  //  bool mJobsStatusSortOrder = false;
  //  QString mJobsSort = "byDate";

  //  QMutexLocker locker(&mMutex);

  QMutexLocker locker(&mJobsSortMutex);

  // don't sort when quitting or stopping all transfers
  if (mAppQuittingStatus) {
    return;
  }
  if (mDoNotSort) {
    return;
  }

  int widgetsCount = ui.jobs->count();
  int move = widgetsCount > 2 ? widgetsCount - 4 : 0;
  QDateTime dt;
  QDateTime widgetStartDateTime;
  QString widgetStatus = 0;
  QString ws;

  for (int i = 0; i < (widgetsCount - 2) / 2 - 1; i = i + 1) {

    for (int j = widgetsCount - 4; j >= i * 2; j = j - 2) {

      QWidget *widget = ui.jobs->itemAt(j)->widget();

      if (auto transfer = qobject_cast<JobWidget *>(widget)) {
        widgetStartDateTime = transfer->getStartDateTime();
        widgetStatus = transfer->getStatus();
      }

      if (mJobsSort == "byDate") {
        if (j == widgetsCount - 4) {
          move = j;
          dt = widgetStartDateTime;
        } else {
          if (mJobsTimeSortOrder) {
            if (dt > widgetStartDateTime) {
              move = j;
              dt = widgetStartDateTime;
            }
          } else {
            if (dt < widgetStartDateTime) {
              move = j;
              dt = widgetStartDateTime;
            }
          }
        }
      }

      if (mJobsSort == "byStatus") {

        if (j == widgetsCount - 4) {
          move = j;
          ws = widgetStatus;
        } else {
          if (mJobsStatusSortOrder) {
            if (ws > widgetStatus) {
              move = j;
              ws = widgetStatus;
            }
          } else {
            if (ws < widgetStatus) {
              move = j;
              ws = widgetStatus;
            }
          }
        }
      }

    } // for (int j

    // move to top
    QWidget *widget = ui.jobs->itemAt(move)->widget();
    QWidget *widget_line = ui.jobs->itemAt(move + 1)->widget();
    auto line = qobject_cast<QFrame *>(widget_line);

    if (auto transfer = qobject_cast<JobWidget *>(widget)) {
      ui.jobs->removeWidget(transfer);
      ui.jobs->removeWidget(line);
      ui.jobs->insertWidget(i * 2, transfer);
      ui.jobs->insertWidget(i * 2 + 1, line);
    } else {
      //
      break;
    }
  } //  for (int i

  return;
}
