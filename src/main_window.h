#pragma once
#include "icon_cache.h"
#include "job_options.h"
#include "pch.h"
#include "ui_main_window.h"
#ifdef Q_OS_MACOS
#include "mac_os_power_saving.h"
#endif

class JobWidget;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow();
  ~MainWindow();

private slots:
  void rcloneGetVersion();
  void rcloneListRemotes();

  void addTransfer(const QString &message, const QString &source,
                   const QString &dest, const QStringList &args,
                   const QString &uniqueId, const QString &transferMode,
                   const QString &requestId);

  void runScript(const QString &script);

  // ARMGDDN Browser: run update.bat (config check on start) and wait for it
  void runConfigCheckOnStart();

  // ARMGDDN Browser: a download hit a quota / rate limit - offer to retry from
  // a sibling mirror in the same folder.
  void offerMirrorRetry(const QString &source, const QString &dest,
                        const QStringList &args);

  void slotCloseTab(int index);

  // quit RB but only when all processes finished
  void quitApp(void);

private:
  Ui::MainWindow ui;

  QSystemTrayIcon mSystemTray;
  // Qt 6 removed QSound; QSoundEffect replaces it for the finished-transfer
  // notification chime (only when the Multimedia module is available).
#ifdef RB_HAVE_MULTIMEDIA
  QSoundEffect mNotificationSound;
#endif
  JobWidget *mLastFinished = nullptr;

  bool mAlwaysShowInTray;
  bool mCloseToTray;
  bool mNotifyFinishedTransfers;
  bool mSoundNotif;

  QLabel *mStatusMessage;

  IconCache mIcons;

  bool mFirstTime = true;
  int mJobCount = 0;

  // keep track of number of active transfers
  int mTransferJobCount = 0;

  // make logic aware that app is quiting
  bool mAppQuittingStatus = false;

  // don't sort then stopping all transfers
  bool mDoNotSort = false;

  bool canClose();
  void closeEvent(QCloseEvent *ev) override;
  bool getConfigPassword(QProcess *p);

  // sort QListWidget view/selection
  QList<QListWidgetItem *> sortListWidget(const QList<QListWidgetItem *> &list,
                                          bool sortOrder = false);

  void addEmptyJobsMessage();

  QIcon mUploadIcon;
  QIcon mDownloadIcon;
  QIcon mMountIcon;
  QMessageBox *mQuittingErrorMsgBox = NULL;

#ifdef Q_OS_MACOS
  MacOsPowerSaving *mMacOsPowerSaving;
#endif

  // prevent race conditions
  QMutex mMutex;
  QMutex mJobsSortMutex;

  // if waiting for processes we show dialog - this is used to calculate delay
  int mQuitInfoDelay = 0;

  void sortJobs();
  bool mJobsTimeSortOrder = false;
  bool mJobsStatusSortOrder = false;
  QString mJobsSort = "byDate";
  QString mIconsLayout;
};
