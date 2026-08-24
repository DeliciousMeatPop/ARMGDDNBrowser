#pragma once

#include "pch.h"

// ARMGDDN Browser: a small, process-wide, disk-backed cache of folder sizes.
//
// Computing a folder's size means running a recursive `rclone size`, which is
// slow and - because the in-memory result dies with the model - was redone on
// every launch and every Refresh. This cache persists results across sessions
// and, crucially, is keyed on a *mirror-independent* identity so that identical
// mirrors share results: every PC mirror is the same library, so a size
// computed once for a versioned game folder is reused by all PC mirrors (and
// likewise for PCVR, etc.). Keys look like "PC|A Quiet Place v16484601
// -ARMGDDN"; see ItemModel for how they are derived.
//
// Entries are only ever added when a real `rclone size` completes, and dropped
// on an explicit Refresh of that subtree, so a hit always reflects a genuine
// prior measurement.
class SizeCache : public QObject {
  Q_OBJECT
public:
  static SizeCache &instance();

  // Look up a key; returns true and fills bytes on a hit.
  bool get(const QString &key, quint64 &bytes);
  // Record a freshly measured size and schedule a save.
  void put(const QString &key, quint64 bytes);
  // Drop every entry whose key starts with prefix (used on Refresh so a folder
  // that may have changed is re-measured).
  void invalidatePrefix(const QString &prefix);

private:
  explicit SizeCache(QObject *parent = nullptr);
  void ensureLoaded();
  void scheduleSave();
  void save();

  QHash<QString, quint64> mMap;
  QString mPath;
  QTimer mSaveTimer;
  bool mLoaded = false;
};
