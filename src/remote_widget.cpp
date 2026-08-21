#include "remote_widget.h"
#include "export_dialog.h"
#include "global.h"
#include "icon_cache.h"
#include "item_model.h"
#include "list_of_job_options.h"
#include "progress_dialog.h"
#include "remote_folder_dialog.h"
#include "transfer_dialog.h"
#include "utils.h"

RemoteWidget::RemoteWidget(IconCache *iconCache, const QString &remote,
                           const QString &remoteType, QWidget *parent)
    : QWidget(parent) {

  ui.setupUi(this);

  ui.elidedMeasure->hide();

  bool isLocal = remoteType == "local";
  mRemoteType = remoteType;

  QString root = isLocal ? "/" : QString();

  QString remoteMode = "main";
  //  QString remoteMode_ = "main";

  auto settings = GetSettings();

#ifndef Q_OS_WIN
  isLocal = false;
#endif

#ifdef Q_OS_WIN
  // check if required rclone version
  QString rcloneVersion = settings->value("Settings/rcloneVersion").toString();
  unsigned int rcloneVersionResult =
      compareVersion(rcloneVersion.toStdString(), "1.50");

  // as with Fusion style in Windows QTreeView font size does not scale
  // with QApplication::font() changes we control it manually using style sheet

  QFont defaultFont = QApplication::font();
  int fontSize = defaultFont.pointSize() + 3;

  QString fontStyleSheet =
      QString("QTreeView { font-size: %1px;}").arg(fontSize);
  ui.tree->setStyleSheet(fontStyleSheet);
#endif

  if (settings->value("Settings/preemptiveLoading").toBool()) {
    mPreemptiveLoading = true;
  } else {
    mPreemptiveLoading = false;
  }

  int preemptiveLoadingLevel =
      settings->value("Settings/preemptiveLoadingLevel").toInt();
  if (preemptiveLoadingLevel == 0) {
    mMaxRcloneLsProcessCount = 10;
  }
  if (preemptiveLoadingLevel == 1) {
    mMaxRcloneLsProcessCount = 20;
  }
  if (preemptiveLoadingLevel == 2) {
    mMaxRcloneLsProcessCount = 40;
  }

  QString buttonStyle = settings->value("Settings/buttonStyle").toString();
  QString buttonSize = settings->value("Settings/buttonSize").toString();
  QString iconsColour = settings->value("Settings/iconsColour").toString();
  settings->setValue("Settings/remoteMode", "main");
  ui.tree->setAlternatingRowColors(
      settings->value("Settings/rowColors", false).toBool());

  QString img_add = "";

  if (iconsColour == "white") {
    img_add = "_inv";
  }

  ui.refresh->setIcon(
      QIcon(":media/images/qbutton_icons/refresh" + img_add + ".png"));
  ui.download->setIcon(
      QIcon(":media/images/qbutton_icons/download" + img_add + ".png"));
  ui.getSize->setIcon(
      QIcon(":media/images/qbutton_icons/getsize" + img_add + ".png"));
  ui.getTree->setIcon(
      QIcon(":media/images/qbutton_icons/gettree" + img_add + ".png"));
  ui.link->setIcon(
      QIcon(":media/images/qbutton_icons/link" + img_add + ".png"));
  ui.export_->setIcon(
      QIcon(":media/images/qbutton_icons/export" + img_add + ".png"));
  ui.getInfo->setIcon(
      QIcon(":media/images/qbutton_icons/info" + img_add + ".png"));

  ui.buttonRefresh->setDefaultAction(ui.refresh);
  ui.buttonDownload->setDefaultAction(ui.download);
  ui.buttonSize->setDefaultAction(ui.getSize);
  ui.buttonTree->setDefaultAction(ui.getTree);
  ui.buttonLink->setDefaultAction(ui.link);
  ui.buttonExport->setDefaultAction(ui.export_);
  ui.buttonInfo->setDefaultAction(ui.getInfo);

  // buttons and icons size
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

  // ARMGDDN Browser: only read-only browsing buttons remain.
  QList<QToolButton *> browseButtons{ui.buttonRefresh, ui.buttonDownload,
                                     ui.buttonSize,    ui.buttonTree,
                                     ui.buttonLink,    ui.buttonExport,
                                     ui.buttonInfo};

  for (QToolButton *b : browseButtons) {
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
  ui.buttonDownload->setMinimumWidth(button_width * 1.4);

  ui.refresh->setStatusTip("Refresh (F5)");
  ui.download->setStatusTip("Download files/directories (ALT-d)");
  ui.getSize->setStatusTip("Get items size - rclone size");
  ui.getTree->setStatusTip("Show directory tree - rclone tree");
  ui.link->setStatusTip("Fetch public link - rclone link");
  ui.export_->setStatusTip("Export files' list");
  ui.getInfo->setStatusTip("Get remote info - rclone about");

  ui.tree->sortByColumn(0, Qt::AscendingOrder);
  ui.tree->header()->setSectionsMovable(false);

  model = new ItemModel(iconCache, remote, this);
  ui.tree->setModel(model);
  QTimer::singleShot(0, ui.tree, SLOT(setFocus()));

  connect(ui.tree->selectionModel(),
          SIGNAL(selectionChanged(QItemSelection, QItemSelection)),
          SLOT(processSelection(QItemSelection, QItemSelection)));

  QObject::connect(model, &QAbstractItemModel::layoutChanged, this, [=]() {
    ui.tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui.tree->resizeColumnToContents(1);
    ui.tree->resizeColumnToContents(2);
  });

  QObject::connect(ui.tree, &QAbstractItemView::clicked, this, [=]() {
    // not used now

    //    QModelIndex index;
    //    QModelIndexList selection = ui.tree->selectionModel()->selectedRows();
    //    index = selection.at(0);

    //    qDebug() << "index: " << index;
  });

  QObject::connect(
      ui.tree, &QTreeView::expanded, this, [=](const QModelIndex &index) {
        if (!mPreemptiveLoading) {
          return;
        }

        // preemptive loading
        QMutexLocker locker(&preemptiveLoadingProcessorMutex);

        if (!mPreemptiveLoadingListDoneNodes.contains(index)) {
          mPreemptiveLoadingListDoneNodes.append(index);
        }

        if (!mPreemptiveLoadingListDone.contains(index)) {

          if (model->isLoading(model->index(0, 0, index))) {
            mPreemptiveLoadingListPending.append(index);
            mPreemptiveLoadingListDone.append(index);
          } else {
            mPreemptiveLoadingListDone.append(index);
            // preload children
            for (int i = 0; i < model->rowCount(index); ++i) {
              if (model->isFolder(model->index(i, 0, index))) {
                mPreemptiveLoadingList.append(model->index(i, 0, index));
                mPreemptiveLoadingListDups = true;
              }
            }
          }
        }

        /*
              QModelIndex parentIndex = index.parent();
                if (!mPreemptiveLoadingListDone.contains(parentIndex)) {
                  // preload peers
                  for (int i = 0; i < model->rowCount(parentIndex); ++i) {
                    if (model->isFolder(model->index(i, 0, parentIndex))) {
                      mPreemptiveLoadingList.append(model->index(i, 0,
           parentIndex)); mPreemptiveLoadingListDups = true;
                    }
                  }
                  mPreemptiveLoadingListDone.append(parentIndex);
                }
        */

        QTimer::singleShot(0, this, SLOT(preemptiveLoadingProcessor()));
      });

  QObject::connect(
      ui.tree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
      [=]() {
        QModelIndex index;
        QModelIndexList selection = ui.tree->selectionModel()->selectedRows();

        int multiSelectCount = selection.count();

        if (selection.isEmpty()) {
          for (auto child : findChildren<QAction *>()) {
            child->setDisabled(true);
          }
          ui.getInfo->setDisabled(false);
          ui.path->clear();
          return;
        }

        if (multiSelectCount > 1) {
          for (auto child : findChildren<QAction *>()) {
            child->setDisabled(true);
          }
          ui.refresh->setDisabled(false);
          ui.download->setDisabled(false);
          ui.getSize->setDisabled(false);
          ui.getInfo->setDisabled(false);
          ui.path->clear();
          return;
        }

        // there is only one item selected
        index = selection.at(0);

        bool topLevel = model->isTopLevel(index);
        bool isFolder = model->isFolder(index);
        QDir path;

        if (model->isLoading(index)) {
          ui.refresh->setDisabled(true);
          ui.download->setDisabled(true);
          ui.getSize->setDisabled(true);
          ui.getTree->setDisabled(true);
          ui.link->setDisabled(true);
          ui.export_->setDisabled(true);
          ui.getInfo->setDisabled(false);
          path = model->path(model->parent(index));

        } else {

          ui.refresh->setDisabled(false);
          ui.download->setDisabled(false);
          ui.getSize->setDisabled(false);
          ui.getTree->setDisabled(!isFolder);
          ui.link->setDisabled(topLevel);
          ui.export_->setDisabled(!isFolder);
          ui.getInfo->setDisabled(false);

          path = model->path(index);
        }

        ui.path->setText(
            remote + ":" +
            (isLocal ? QDir::toNativeSeparators(path.path()) : path.path()));
      });

  // QObject::connect(ui.refresh
  QObject::connect(ui.refresh, &QAction::triggered, this, [=]() {
    setRemoteMode(0, remoteType);

    QModelIndexList multiSelection = ui.tree->selectionModel()->selectedRows();
    int multiSelectCount = multiSelection.count();

    if (multiSelectCount < 2) {
      QModelIndex index = ui.tree->selectionModel()->selectedRows().front();
      model->refresh(index);
    } else {

      bool parentRefreshed = false;
      for (int i = 0; i < multiSelectCount; ++i) {
        QModelIndex multiIndex = multiSelection.at(i);
        if (model->isFolder(multiIndex)) {
          model->refresh(multiIndex);
        } else {
          // refresh parent folder only once
          if (!parentRefreshed) {
            model->refresh(multiIndex);
            parentRefreshed = true;
          };
        }
      }
    }
  });

  // QObject::connect(ui.link
  QObject::connect(ui.link, &QAction::triggered, this, [=]() {
    setRemoteMode(0, remoteType);

    // Elided....Text base measure
    // progress dialog uses the same fonts
    QFontMetrics metrix(ui.elidedMeasure->font());

    QString toolTip;

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();

    QString path = model->path(index).path();

    QString pathMsg =
        metrix.elidedText((isLocal ? QDir::toNativeSeparators(path) : path),
                          Qt::ElideMiddle, 500);

    toolTip = "\"" + remote + ":" +
              (isLocal ? QDir::toNativeSeparators(path) : path) + "\"";

    QProcess *process = new QProcess;
    UseRclonePassword(process);
    process->setProgram(GetRclone());
    process->setArguments(QStringList()
                          << "link" << GetRcloneConf()
                          << GetRemoteModeRcloneOptions()
                          << GetDefaultOptionsList("defaultRcloneOptions")
                          << remote + ":" + path);
    process->setProcessChannelMode(QProcess::MergedChannels);
    ProgressDialog *progress =
        new ProgressDialog("Fetch Public Link", "Running... ",
                           QString("Public link for: ") + "\"" +
                               metrix.elidedText(remote, Qt::ElideMiddle, 150) +
                               ":" + pathMsg + "\"",
                           process, NULL, false, true, toolTip);
    progress->expand();
    progress->allowToClose();
    progress->show();
  });

  //!!! QObject::connect(ui.download
  QObject::connect(ui.download, &QAction::triggered, this, [=]() {
    QString _remoteMode =
        setRemoteMode(0, remoteType);

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();
    QDir path = model->path(index);

    QModelIndexList multiSelection = ui.tree->selectionModel()->selectedRows();
    int multiSelectCount = multiSelection.count();
    QStringList includedList;
    bool isMultiselect = false;

    if (multiSelectCount > 1) {

      // selection control makes sure that all are on the same level so we can
      // use any to derived download root

      isMultiselect = true;
      path = model->path(index.parent());

      includedList = getSelectionFilteringPatterns(multiSelection);
    }

    TransferDialog t(true, false, remote, path, model->isFolder(index),
                     remoteType, _remoteMode, isMultiselect, includedList,
                     this);
    if (t.exec() == QDialog::Accepted) {

      if (t.getDryRun() || t.getTaskId() == "") {

        QString src = t.getSource();
        QString dst = t.getDest();
        QStringList args = t.getOptions();
        QString info;

        if (t.getDryRun()) {
          args << "--dry-run";
          info = QString("Dry run, %1 from %2").arg(t.getMode()).arg(src);
        } else {
          info = QString("%1 from %2").arg(t.getMode()).arg(src);
        }

        emit addTransfer(info, src, dst, args, QUuid::createUuid().toString(),
                         "", QUuid::createUuid().toString());

      } else {
        emit addSavedTransfer(t.getTaskId(), t.getDryRun(), t.getAddToQueue());
      }
    }
  });

  //!!! QObject::connect(ui.getTree
  QObject::connect(ui.getTree, &QAction::triggered, this, [=]() {
    setRemoteMode(0, remoteType);

    // Elided....Text base measure
    // progress dialog uses the same fonts
    QFontMetrics metrix(ui.elidedMeasure->font());

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();

    QString path = model->path(index).path();
    QString pathMsg =
        metrix.elidedText((isLocal ? QDir::toNativeSeparators(path) : path),
                          Qt::ElideMiddle, 500);

    QString toolTip = "\"" + remote + ":" +
                      (isLocal ? QDir::toNativeSeparators(path) : path) + "\"";

    QProcess *process = new QProcess;
    UseRclonePassword(process);
    process->setProgram(GetRclone());
    process->setArguments(
        QStringList() << "tree"
                      << "-d" << GetRcloneConf() << GetRemoteModeRcloneOptions()
                      << GetDefaultOptionsList("defaultRcloneOptions")
                      << remote + ":" + path);
    process->setProcessChannelMode(QProcess::MergedChannels);
    ProgressDialog *progress =
        new ProgressDialog("Show directories tree", "Running... ",
                           QString("rclone tree -d ") + "\"" +
                               metrix.elidedText(remote, Qt::ElideMiddle, 150) +
                               ":" + pathMsg + "\"",
                           process, NULL, false, false, toolTip);
    progress->expand();
    progress->allowToClose();
    //    progress->resize(566, 350);
    progress->show();
  });

  //!!! QObject::connect(ui.getSize
  QObject::connect(ui.getSize, &QAction::triggered, this, [=]() {
    setRemoteMode(0, remoteType);

    QString progressMsg;
    QModelIndexList multiSelection = ui.tree->selectionModel()->selectedRows();
    int multiSelectCount = multiSelection.count();

    // Elided....Text base measure
    // progress dialog uses the same fonts
    QFontMetrics metrix(ui.elidedMeasure->font());

    if (multiSelectCount == 0)
      return;

    QString toolTip;
    QStringList includedList;
    QStringList includedListFinal;

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();
    QString path = model->path(index).path();

    QString pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;
    QProcess *process = new QProcess;

    if (multiSelectCount > 1) {

      path = model->path(index.parent()).path();
      pathMsg = isLocal ? QDir::toNativeSeparators(path) : path;
      progressMsg =
          QString("Size of selected %1 items in ").arg(multiSelectCount) +
          "\"" + metrix.elidedText(remote, Qt::ElideMiddle, 150) + ":" +
          metrix.elidedText(pathMsg, Qt::ElideMiddle, 500) + "\"";

      includedList = getSelectionFilteringPatterns(multiSelection);

      // as we run rclone directly here we have to add --filter
      for (int i = 0; i < includedList.count(); ++i) {
        includedListFinal << "--filter";
        includedListFinal << "+ " + includedList.at(i);
      }
      includedListFinal << "--filter"
                        << "- *";

    } else {

      toolTip = "\"" + remote + ":" + pathMsg + "\"";

      progressMsg = QString("Size of ") + "\"" +
                    metrix.elidedText(remote, Qt::ElideMiddle, 150) + ":" +
                    metrix.elidedText(pathMsg, Qt::ElideMiddle, 500) + "\"";

    } // if (multiSelectCount > 1)

    UseRclonePassword(process);
    process->setProgram(GetRclone());
    process->setArguments(QStringList()
                          << "size" << GetRcloneConf()
                          << GetRemoteModeRcloneOptions()
                          << GetDefaultOptionsList("defaultRcloneOptions")
                          << remote + ":" + path << includedListFinal);
    process->setProcessChannelMode(QProcess::MergedChannels);

    ProgressDialog *progress =
        new ProgressDialog("Get Size", "Running... ", progressMsg, process,
                           NULL, false, false, toolTip);

    progress->expand();
    progress->allowToClose();
    progress->show();
  });

  //!!! Object::connect(ui.export
  QObject::connect(ui.export_, &QAction::triggered, this, [=]() {
    setRemoteMode(0, remoteType);

    // Elided....Text base measure
    // progress dialog uses the same fonts
    QFontMetrics metrix(ui.elidedMeasure->font());

    QString toolTip;

    QModelIndex index = ui.tree->selectionModel()->selectedRows().front();

    QString path_info = model->path(index).path();
    QString pathMsg = isLocal ? QDir::toNativeSeparators(path_info) : path_info;

    QDir path = model->path(index);

    ExportDialog e(remote, path, this);

    if (e.exec() == QDialog::Accepted) {
      QString dst = e.getDestination();
      bool txt = e.onlyFilenames();

      QFile *file = new QFile(dst);
      if (!file->open(QFile::WriteOnly)) {
        QMessageBox::warning(
            this, "Error",
            QString("Cannot open file '%1' for writing!").arg(dst));
        delete file;
        return;
      }

      QRegularExpression re(QRegularExpression::anchoredPattern(
          R"(\s*(\d+) (\d\d\d\d-\d\d-\d\d \d\d:\d\d:\d\d)\.\d+ (.+))"));

      QProcess *process = new QProcess;
      UseRclonePassword(process);
      process->setProgram(GetRclone());
      process->setArguments(QStringList()
                            << GetRcloneConf() << GetRemoteModeRcloneOptions()
                            << GetDefaultOptionsList("defaultRcloneOptions")
                            << e.getOptions());
      process->setProcessChannelMode(QProcess::MergedChannels);

      toolTip =
          "\"" + remote + ":" + pathMsg + "\"" + "\nto " + "\"" + dst + "\"";

      ProgressDialog *progress = new ProgressDialog(
          "Export", "Running... ",
          QString("Exporting content of ") + "\"" +
              metrix.elidedText(remote, Qt::ElideMiddle, 150) + ":" +
              metrix.elidedText(pathMsg, Qt::ElideMiddle, 500) + "\"" +
              "\nto " + "\"" + metrix.elidedText(dst, Qt::ElideMiddle, 500) +
              "\"",
          process, NULL, false, false, toolTip);

      file->setParent(progress);

      QObject::connect(progress, &ProgressDialog::outputAvailable, this,
                       [=](const QString &output) {
                         QTextStream out(file);
                         out.setEncoding(QStringConverter::Utf8);

                         for (const auto &line : output.split('\n')) {

                           QString lineTmp = line;
                           lineTmp.replace("\n", "");

                           QRegularExpressionMatch reMatch = re.match(lineTmp);
                           if (reMatch.hasMatch()) {
                             QStringList cap = reMatch.capturedTexts();

                             if (txt) {
                               out << "\"" << cap[3] << "\"" << '\n';
                             } else {
                               QString name = cap[3];
                               out << "\"" << name << "\""
                                   << ","
                                   << "\"" << cap[2] << "\""
                                   << "," << cap[1].toULongLong() << '\n';
                             }
                           }
                         }
                       });

      progress->allowToClose();
      progress->show();
    }
  });

  QObject::connect(ui.getInfo, &QAction::triggered, this, [=]() {
    setRemoteMode(0, remoteType);

    // Elided....Text
    QFontMetrics metrix(ui.elidedMeasure->font());

    QString toolTip = "\"" + remote + ":" + "\"";

    QProcess *process = new QProcess;
    UseRclonePassword(process);
    process->setProgram(GetRclone());
    process->setArguments(QStringList()
                          << "about" << GetRcloneConf()
                          << GetRemoteModeRcloneOptions()
                          << GetDefaultOptionsList("defaultRcloneOptions")
                          << remote + ":");
    process->setProcessChannelMode(QProcess::MergedChannels);

    ProgressDialog *progress = new ProgressDialog(
        "Get remote Info", "Runnning... ",
        "rclone about \"" + metrix.elidedText(remote, Qt::ElideMiddle, 150) +
            ":\"",
        process, NULL, false, false, toolTip);

    progress->expand();
    progress->allowToClose();
    progress->show();
  });



  QObject::connect(ui.tree, &QWidget::customContextMenuRequested, this,
                   [=](const QPoint &pos) {
                     setRemoteMode(0,
                                   remoteType);

                     QMenu menu;
                     menu.addAction(ui.refresh);
                     menu.addSeparator();
                     menu.addAction(ui.download);
                     menu.addSeparator();
                     menu.addAction(ui.getSize);
                     menu.addAction(ui.getTree);
                     menu.addAction(ui.link);
                     menu.addAction(ui.export_);
                     menu.addAction(ui.getInfo);
                     menu.exec(ui.tree->viewport()->mapToGlobal(pos));
                   });

  if (isLocal) {
    QHash<QString, QPersistentModelIndex> drives;

    // QDir::drives is fast
    for (const auto &drive : QDir::drives()) {
      QString path = drive.path();
      QModelIndex index = model->addRoot(QDir::toNativeSeparators(path), path);
      drives.insert(path, index);
    }

#if (QT_VERSION >= QT_VERSION_CHECK(5, 4, 0)) && !(defined Q_OS_WIN)
    QThread *thread = new QThread(this);
    thread->start();

    QObject *worker = new QObject();
    worker->moveToThread(thread);

    QTimer::singleShot(0, worker, [=]() {
      QStorageInfo info;
      info.refresh();

      // QStorageInfo::mountedVolumes is slow :(
      for (const auto &volume : info.mountedVolumes()) {
        QString name = volume.name();
        if (!name.isEmpty()) {
          QString path = volume.rootPath();
          QString item =
              QString("%1 (%2)").arg(QDir::toNativeSeparators(path)).arg(name);
          QTimer::singleShot(0, this,
                             [=]() { model->rename(drives[path], item); });
        }
      }

      thread->quit();
      thread->deleteLater();
      worker->deleteLater();
    });
#endif

    ui.tree->selectionModel()->selectionChanged(QItemSelection(),
                                                QItemSelection());
  } else {
    QModelIndex index = model->addRoot("/", root);
    ui.tree->selectionModel()->select(
        index, QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
    mRootIndex = index;

    mPreemptiveLoadingListDone.append(index);

    ui.tree->expand(index);
    QTimer::singleShot(200, Qt::CoarseTimer, this, SLOT(initialModelLoading()));
  }

  QShortcut *close = new QShortcut(QKeySequence::Close, this);
  QObject::connect(close, &QShortcut::activated, this, [=]() {
    auto tabs = qobject_cast<QTabWidget *>(parent);
    tabs->removeTab(tabs->indexOf(this));
  });

}

RemoteWidget::~RemoteWidget() {}

QString setRemoteMode(int index, QString remoteType) {

  QString mode = "main";

  if (remoteType == "drive") {

    auto settings = GetSettings();
    switch (index) {

    case 0:
      settings->setValue("Settings/remoteMode", "main");
      mode = "main";
      break;
    case 1:
      settings->setValue("Settings/remoteMode", "shared");
      mode = "shared";
      break;
    case 2:
      settings->setValue("Settings/remoteMode", "trash");
      mode = "trash";
      break;
    }
  }

  return mode;
}

void RemoteWidget::initialModelLoading() {
  QMutexLocker locker(&preemptiveLoadingProcessorMutex);
  setRemoteMode(0, mRemoteType);

  // model and mRootIndex in private
  QModelIndex index = mRootIndex;

  if (model->rowCount(index) == 1) {
    // try again later
    QTimer::singleShot(200, Qt::CoarseTimer, this, SLOT(initialModelLoading()));
  } else {

    for (int i = 0; i < model->rowCount(index); ++i) {
      if (model->isFolder(model->index(i, 0, index))) {
        mPreemptiveLoadingList.append(model->index(i, 0, index));
      }
    }

    QTimer::singleShot(0, this, SLOT(preemptiveLoadingProcessor()));
  }
  return;
}

void RemoteWidget::preemptiveLoadingProcessor() {

  QMutexLocker locker(&preemptiveLoadingProcessorMutex);

  // don't do preloading for local drives
  if (mRemoteType == "local") {
    clearPreemptiveQueues();
    return;
  }

  if (!mPreemptiveLoading) {
    return;
  }

  // update aggressivness of preloading every few secs
  mCountLevel++;
  if ((mCountLevel % 30) == 0) {

    mCountLevel = 1;

    auto settings = GetSettings();

    if (settings->value("Settings/preemptiveLoading").toBool()) {
      mPreemptiveLoading = true;
    } else {
      mPreemptiveLoading = false;
    }

    int preemptiveLoadingLevel =
        settings->value("Settings/preemptiveLoadingLevel").toInt();

    if (preemptiveLoadingLevel == 0) {
      mMaxRcloneLsProcessCount = 10;
    }
    if (preemptiveLoadingLevel == 1) {
      mMaxRcloneLsProcessCount = 20;
    }
    if (preemptiveLoadingLevel == 2) {
      mMaxRcloneLsProcessCount = 40;
    }
  }

  bool runAgain = false;

  setRemoteMode(0, mRemoteType);

  QModelIndex tmpIndex;
  QModelIndexList tmpList;
  int tmpCount;

  tmpCount = mPreemptiveLoadingListPending.count();

  if (tmpCount > 0) {

    for (int i = tmpCount - 1; i >= 0; i--) {

      tmpIndex = mPreemptiveLoadingListPending.at(i);

      if (!model->isLoading(model->index(0, 0, tmpIndex))) {
        tmpIndex = mPreemptiveLoadingListPending.takeAt(i);
        // preload children
        for (int j = 0; j < model->rowCount(tmpIndex); ++j) {
          if (model->isFolder(model->index(i, 0, tmpIndex))) {
            mPreemptiveLoadingList.append(model->index(j, 0, tmpIndex));
            mPreemptiveLoadingListDups = true;
          }
        }
      }
    }
  }

  tmpList.clear();
  tmpCount = mPreemptiveLoadingList.count();

  if (tmpCount > 0) {
    if (global.rcloneLsProcessCount < mMaxRcloneLsProcessCount) {

      // remove duplicates if new indexes were added
      if (mPreemptiveLoadingListDups) {
        for (int i = tmpCount - 1; i >= 0; --i) {
          tmpIndex = mPreemptiveLoadingList.at(i);
          if (!tmpList.contains(tmpIndex)) {
            tmpList.prepend(tmpIndex);
          }
        }
        mPreemptiveLoadingList = tmpList;
        mPreemptiveLoadingListDups = false;
      }

      // one refresh will trigger two rclone processes - lsl and lsd
      int rcloneLsProcessesFreeCount =
          (mMaxRcloneLsProcessCount - global.rcloneLsProcessCount) / 2;

      for (int i = 0; i < rcloneLsProcessesFreeCount; ++i) {
        if (rcloneLsProcessesFreeCount <= 0) {
          break;
        }

        tmpCount = mPreemptiveLoadingList.count();
        for (int j = 0; j < tmpCount; ++j) {
          if (rcloneLsProcessesFreeCount <= 0) {
            break;
          }

          // process from the bottom so last clicked folder is processed first
          QModelIndex index =
              mPreemptiveLoadingList.takeAt(mPreemptiveLoadingList.count() - 1);

          if (!mPreemptiveLoadingListDone.contains(index)) {

            if (!mPreemptiveLoadingListDoneNodes.contains(index)) {
              // force model update
              // model->refresh(index);
              model->index(1, 0, index).isValid();
              rcloneLsProcessesFreeCount--;
              mPreemptiveLoadingListDoneNodes.append(index);
            }
          }
        }
      }
      runAgain = true;
    } else {
      runAgain = true;
    }
  } else {
    if (mPreemptiveLoadingListPending.count() > 0) {
      runAgain = true;
    }
  }

  if (runAgain) {
    QTimer::singleShot(300, Qt::CoarseTimer, this,
                       SLOT(preemptiveLoadingProcessor()));
  }

  return;
}

void RemoteWidget::switchRemoteType() {
  QMutexLocker locker(&preemptiveLoadingProcessorMutex);

  // clear preemptive loading lists
  clearPreemptiveQueues();

  // we can only switch when pending preemptive loading jobs are finished and
  // root is not reloading
  if (global.rcloneLsProcessCount == 0 &&
      !model->isLoading(model->index(0, 0, mRootIndex))) {

    mCount = 0;

    setRemoteMode(0, mRemoteType);

    //!!!!!!!!! ???
    // clear top folder's rows
    while (model->removeRow(0, mRootIndex)) {
    }

    ui.tree->selectionModel()->clear();
    ui.tree->selectionModel()->select(
        mRootIndex, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    model->refresh(mRootIndex);
    QTimer::singleShot(0, ui.tree, SLOT(setFocus()));

    ui.tree->showColumn(0);
    ui.tree->showColumn(1);
    ui.tree->showColumn(2);
    ui.path->setAlignment(Qt::AlignLeft);
    ui.path->clear();
    mPreemptiveLoadingListDone.append(mRootIndex);
    QTimer::singleShot(200, Qt::CoarseTimer, this, SLOT(initialModelLoading()));

  } else {

    // there are still rclone ls jobs running - we wait
    mCount++;
    ui.path->setAlignment(Qt::AlignHCenter);

    if (mCount % 2 == 0) {
      if (ui.path->text().isEmpty()) {
        ui.path->setText(
            "*****    Please wait - finishing background jobs    *****");
      } else {
        ui.path->clear();
      }
    }

    QTimer::singleShot(300, Qt::CoarseTimer, this, SLOT(switchRemoteType()));
  }
}

void RemoteWidget::processSelection(const QItemSelection &selected,
                                    const QItemSelection &deselected) {
  if (deselected.empty()) {
  }

  if (selected.empty())
    return;

  QItemSelectionModel *selectionModel = ui.tree->selectionModel();
  QItemSelection selection = selectionModel->selection();

  const QModelIndex parent = ui.tree->currentIndex().parent();
  const QModelIndex currentItem = ui.tree->currentIndex();

  //  QModelIndexList multiSelectionRows =
  //      ui.tree->selectionModel()->selectedRows();
  //  int multiSelectCount = multiSelectionRows.count();

  QItemSelection invalid;
  QItemSelection valid;

  Q_FOREACH (QModelIndex index, selection.indexes()) {

    if (index.parent() == parent) {
      valid.select(index, index);
      continue;
    }

    invalid.select(index, index);
  }

  // for remotes with many roots (e.g. Windows with more than one drive) we dont
  // allow selecting more than one

  if (model->isTopLevel(currentItem)) {

    Q_FOREACH (QModelIndex multiIndex, valid.indexes()) {

      if (multiIndex.row() == currentItem.row())
        continue;

      if (model->isTopLevel(multiIndex)) {
        invalid.select(multiIndex, multiIndex);
      }
    }
  }

  selectionModel->select(invalid, QItemSelectionModel::Deselect);
}

QStringList RemoteWidget::getSelectionFilteringPatterns(
    const QModelIndexList &multiSelection) {

  QStringList includePatternsList;

  int multiSelectionCount = multiSelection.count();

  if (multiSelectionCount == 0) {
    return includePatternsList;
  }

  for (int i = 0; i < multiSelectionCount; ++i) {
    QModelIndex index = multiSelection.at(i);
    QDir path = model->path(index);

    /*
     *  rclone filtering uses its own patterns parser
     *  we have to esacape ? [ ] { } * \
     *  otherwise items containing these characters will be missed
     */
    QString itemName = path.dirName();
    itemName.replace("\\", "\\\\");
    itemName.replace("[", "\\[");
    itemName.replace("]", "\\]");
    itemName.replace("?", "\\?");
    itemName.replace("{", "\\{");
    itemName.replace("}", "\\}");
    itemName.replace("*", "\\*");

    if (model->isFolder(index)) {
      // directory
      includePatternsList << "/" + itemName + "/**";
    } else {
      // file
      includePatternsList << "/" + itemName;
    }
  }

  return includePatternsList;
}

void RemoteWidget::refreshAfterMove() {

  if (model->isLoading(model->index(0, 0, mSrcIndex))) {

    QTimer::singleShot(300, Qt::CoarseTimer, this, SLOT(refreshAfterMove()));

  } else {

    // check if any parent is loading
    // start with mDestIndex and go to the top;
    QModelIndex top = mDestIndex;
    bool isAnyParentLoading = false;
    while (!model->isTopLevel(top)) {
      if (model->isLoading(model->index(0, 0, top))) {
        isAnyParentLoading = true;
      }
      top = top.parent();
    }

    if (!isAnyParentLoading) {
      model->refresh(mDestIndex);
    }
  }

  return;
}

void RemoteWidget::clearPreemptiveQueues() {

  // clear preemptive loading lists
  mPreemptiveLoadingList.clear();
  mPreemptiveLoadingListDone.clear();
  mPreemptiveLoadingListDoneNodes.clear();
  mPreemptiveLoadingList.clear();
  mPreemptiveLoadingListPending.clear();

  return;
}
