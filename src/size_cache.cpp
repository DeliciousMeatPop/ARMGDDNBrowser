#include "size_cache.h"
#include "utils.h"

SizeCache &SizeCache::instance() {
  static SizeCache cache;
  return cache;
}

SizeCache::SizeCache(QObject *parent) : QObject(parent) {
  mPath = QDir(GetAppDir()).filePath("ARMGDDNBrowser.sizecache.json");
  mSaveTimer.setSingleShot(true);
  mSaveTimer.setInterval(2000);
  QObject::connect(&mSaveTimer, &QTimer::timeout, this, [this]() { save(); });
}

void SizeCache::ensureLoaded() {
  if (mLoaded) {
    return;
  }
  mLoaded = true;

  QFile f(mPath);
  if (!f.open(QIODevice::ReadOnly)) {
    return;
  }
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
  f.close();
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return;
  }
  const QJsonObject obj = doc.object();
  for (auto it = obj.begin(), end = obj.end(); it != end; ++it) {
    // JSON numbers are doubles; byte counts stay well within exact range.
    mMap.insert(it.key(), static_cast<quint64>(it.value().toDouble()));
  }
}

bool SizeCache::get(const QString &key, quint64 &bytes) {
  if (key.isEmpty()) {
    return false;
  }
  ensureLoaded();
  auto it = mMap.constFind(key);
  if (it == mMap.constEnd()) {
    return false;
  }
  bytes = it.value();
  return true;
}

void SizeCache::put(const QString &key, quint64 bytes) {
  if (key.isEmpty()) {
    return;
  }
  ensureLoaded();
  auto it = mMap.constFind(key);
  if (it != mMap.constEnd() && it.value() == bytes) {
    return; // unchanged - no need to rewrite
  }
  mMap.insert(key, bytes);
  scheduleSave();
}

void SizeCache::invalidatePrefix(const QString &prefix) {
  if (prefix.isEmpty()) {
    return;
  }
  ensureLoaded();
  bool changed = false;
  for (auto it = mMap.begin(); it != mMap.end();) {
    if (it.key().startsWith(prefix)) {
      it = mMap.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  if (changed) {
    scheduleSave();
  }
}

void SizeCache::scheduleSave() {
  if (!mSaveTimer.isActive()) {
    mSaveTimer.start();
  }
}

void SizeCache::save() {
  QJsonObject obj;
  for (auto it = mMap.constBegin(), end = mMap.constEnd(); it != end; ++it) {
    obj.insert(it.key(), static_cast<double>(it.value()));
  }
  QJsonDocument doc(obj);

  // Write atomically so a crash mid-write can't corrupt the cache.
  QSaveFile f(mPath);
  if (!f.open(QIODevice::WriteOnly)) {
    return;
  }
  f.write(doc.toJson(QJsonDocument::Compact));
  f.commit();
}
