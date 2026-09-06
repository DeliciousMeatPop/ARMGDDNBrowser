#pragma once

#include "pch.h"

std::unique_ptr<QSettings> GetSettings();

void ReadSettings(QSettings *settings, QObject *widget);
void WriteSettings(QSettings *settings, QObject *widget);

bool IsPortableMode();

QString GetRclone();
void SetRclone(const QString &rclone);

QStringList GetRcloneConf();
void SetRcloneConf(const QString &rcloneConf);

// ARMGDDN Browser: the rclone binary and config are always looked for next to
// the application executable. The binary is AG(.exe) or rclone(.exe) and the
// config is ag.conf or rclone.conf - no user configuration of these paths.
QString GetAppDir();
QString AutoDetectRclone();
QString AutoDetectRcloneConf();

void UseRclonePassword(QProcess *process);
void SetRclonePassword(const QString &rclonePassword);

QStringList GetDefaultOptionsList(const QString &settingsOptions);
QStringList GetRemoteModeRcloneOptions();

// CODE field flags (Preferences > General > CODE). Space-separated switches
// for people who know them. --staff is an umbrella that turns on the staff
// switches (currently just --dont-nag-me).
QStringList GetCodeFlags();
bool HasCodeFlag(const QString &flag);
QStringList GetShowHidden();
QStringList GetRcloneCmd(const QStringList &args);

QDir GetConfigDir(void);

unsigned int compareVersion(std::string, std::string);
