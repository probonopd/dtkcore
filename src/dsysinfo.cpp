/*
 * Copyright (C) 2017 ~ 2018 Deepin Technology Co., Ltd.
 *
 * Author:     zccrs <zccrs@live.com>
 *
 * Maintainer: zccrs <zhangjide@deepin.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "dsysinfo.h"

#include <QFile>
#include <QLocale>
#include <QStorageInfo>
#include <QProcess>
#include <QDebug>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>

#ifdef Q_OS_LINUX
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

DCORE_BEGIN_NAMESPACE

class DSysInfoPrivate
{
public:
    DSysInfoPrivate();

#ifdef Q_OS_LINUX
    void ensureDeepinInfo();
#endif
    void ensureReleaseInfo();
    void ensureComputerInfo();

#ifdef Q_OS_LINUX
    DSysInfo::DeepinType deepinType = DSysInfo::DeepinType(-1);
    QMap<QString, QString> deepinTypeMap; //Type Name with Language
    QString deepinVersion;
    QString deepinEdition;
    QString deepinCopyright;
#endif

    DSysInfo::ProductType productType = DSysInfo::ProductType(-1);
    QString prettyName;
    QString productTypeString;
    QString productVersion;

    QString computerName;
    QString cpuModelName;
    qint64 memoryTotalSize = -1;
    qint64 diskSize = 0;
};

DSysInfoPrivate::DSysInfoPrivate()
{

}

#ifdef Q_OS_LINUX
void DSysInfoPrivate::ensureDeepinInfo()
{
    if (deepinType >= 0)
        return;

    QFile file("/etc/deepin-version");

    if (!file.open(QFile::ReadOnly)) {
        deepinType = DSysInfo::UnknownDeepin;

        return;
    }

    char buf[1024];
    int buf_length = 0;

    Q_FOREVER {
        buf_length = file.readLine(buf, sizeof(buf));

        if (buf_length < 0)
            break;

        const QByteArray line(buf, buf_length);
        const QByteArrayList &list = line.split('=');

        if (list.count() != 2) {
            continue;
        }

        const auto key_value = qMakePair(list.first().trimmed(), list.last().trimmed());

        if (key_value.first == "Version") {
            deepinVersion = key_value.second;
        } else if (line.startsWith("Type")) {
            if (key_value.first == "Type") {
                deepinTypeMap[QString()] = QString::fromLatin1(key_value.second);
            } else if (key_value.first.at(4) == '[' && key_value.first.at(key_value.first.size() - 1) == ']') {
                const QByteArray &language = key_value.first.mid(5, key_value.first.size() - 6);

                if (!language.isEmpty()) {
                    deepinTypeMap[QString::fromLatin1(language)] = QString::fromUtf8(key_value.second);
                }
            }
        } else if (key_value.first == "Edition") {
            deepinEdition = QString::fromUtf8(key_value.second);
        } else if (key_value.first == "Copyright") {
            deepinCopyright = QString::fromUtf8(key_value.second);
        }

        if (!deepinTypeMap.isEmpty() && !deepinEdition.isEmpty() && !deepinCopyright.isEmpty()) {
            break;
        }
    }

    file.close();

    const QString &deepin_type = deepinTypeMap[QString()];

    if (deepin_type.isEmpty()) {
        deepinType = DSysInfo::UnknownDeepin;
    } else if (deepin_type == "Desktop") {
        deepinType = DSysInfo::DeepinDesktop;
    } else if (deepin_type == "Professional") {
        deepinType = DSysInfo::DeepinProfessional;
    } else {
        deepinType = DSysInfo::UnknownDeepin;
    }
}

static QString unquote(const QByteArray &value)
{
    if (value.at(0) == '"' || value.at(0) == '\'') {
        return QString::fromLatin1(value.mid(1, value.size() - 2));
    }

    return QString::fromLatin1(value);
}

static bool readEtcFile(DSysInfoPrivate *info, const char *filename,
                        const QByteArray &idKey, const QByteArray &versionKey, const QByteArray &prettyNameKey)
{

    QFile file(QString::fromLatin1(filename));

    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    quint8 valid_data_count = 0;
    char buf[1024];

    while (valid_data_count < 3) {
        int buf_length = file.readLine(buf, sizeof(buf));

        if (buf_length < 0)
            break;

        const QByteArray line(buf, buf_length - 1);

        if (info->productTypeString.isEmpty() && line.startsWith(idKey)) {
            const QByteArray value(line.constData() + idKey.size());
            info->productTypeString = unquote(value);
            ++valid_data_count;
            continue;
        }

        if (info->prettyName.isEmpty() && line.startsWith(prettyNameKey)) {
            const QByteArray value(line.constData() + prettyNameKey.size());
            info->prettyName = unquote(value);
            ++valid_data_count;
            continue;
        }

        if (info->productVersion.isEmpty() && line.startsWith(versionKey)) {
            const QByteArray value(line.constData() + versionKey.size());
            info->productVersion = unquote(value);
            ++valid_data_count;
            continue;
        }
    }

    file.close();

    return valid_data_count != 0;
}

static bool readOsRelease(DSysInfoPrivate *info)
{
    if (!readEtcFile(info, "/etc/os-release", "ID=", "VERSION_ID=", "PRETTY_NAME="))
        return readEtcFile(info, "/usr/lib/os-release", "ID=", "VERSION_ID=", "PRETTY_NAME=");

    return true;
}

static bool readLsbRelease(DSysInfoPrivate *info)
{
    return readEtcFile(info, "/etc/lsb-release", "DISTRIB_ID=", "DISTRIB_RELEASE=", "DISTRIB_DESCRIPTION=");
}
#endif

void DSysInfoPrivate::ensureReleaseInfo()
{
    if (productType >= 0) {
        return;
    }

#ifdef Q_OS_LINUX
    readOsRelease(this);
    readLsbRelease(this);

    if (productTypeString.isEmpty()) {
        productType = DSysInfo::UnknownType;
    } else {
        switch (productTypeString.at(0).unicode()) {
        case 'd':
        case 'D':
            if (productTypeString.compare("deepin", Qt::CaseInsensitive) == 0) {
                productType = DSysInfo::Deepin;
            } else if (productTypeString.compare("debian", Qt::CaseInsensitive) == 0) {
                productType = DSysInfo::Debian;
            }
            break;
        case 'a':
        case 'A':
            if (productTypeString.compare("arch", Qt::CaseInsensitive) == 0)
                productType = DSysInfo::ArchLinux;
            break;
        case 'c':
        case 'C':
            if (productTypeString.compare("centos", Qt::CaseInsensitive) == 0)
                productType = DSysInfo::CentOS;
            break;
        case 'f':
        case 'F':
            if (productTypeString.compare("fedora", Qt::CaseInsensitive) == 0)
                productType = DSysInfo::Fedora;
            break;
        case 'l':
        case 'L':
            if (productTypeString.compare("linuxmint", Qt::CaseInsensitive) == 0)
                productType = DSysInfo::LinuxMint;
            break;
        case 'm':
        case 'M':
            if (productTypeString.compare("manjaro", Qt::CaseInsensitive) == 0)
                productType = DSysInfo::Manjaro;
            break;
        case 'o':
        case 'O':
            if (productTypeString.compare("opensuse", Qt::CaseInsensitive) == 0)
                productType = DSysInfo::openSUSE;
            break;
        case 's':
        case 'S':
            if (productTypeString.compare("sailfishos", Qt::CaseInsensitive) == 0)
                productType = DSysInfo::SailfishOS;
            break;
        case 'u':
        case 'U':
            if (productTypeString.compare("ubuntu", Qt::CaseInsensitive) == 0)
                productType = DSysInfo::Ubuntu;
            break;
        default:
            productType = DSysInfo::UnknownType;
            break;
        }
    }
#endif
}

void DSysInfoPrivate::ensureComputerInfo()
{
    if (memoryTotalSize >= 0)
        return;

#ifdef Q_OS_LINUX
    struct utsname u;
    if (uname(&u) == 0)
        computerName = QString::fromLatin1(u.nodename);

    QFile file("/proc/cpuinfo");

    if (file.open(QFile::ReadOnly)) {
        char buf[1024];
        qint64 lineLength = 0;

        do {
            lineLength = file.readLine(buf, sizeof(buf));

            const QByteArray line(buf, lineLength);

            if (line.startsWith("model name")) {
                if (int index = line.indexOf(':', 10)) {
                    if (index > 0)
                        cpuModelName = QString::fromLatin1(line.mid(index + 1).trimmed());
                }
                break;
            }
        } while (lineLength >= 0);

        file.close();
    }

    memoryTotalSize = get_phys_pages() * sysconf(_SC_PAGESIZE);

    // Getting Disk Size
    const QString &deviceName = QStorageInfo::root().device();
    QProcess lsblk;

    lsblk.start("lsblk", {"-Jlpb", "-oNAME,KNAME,PKNAME,SIZE"}, QIODevice::ReadOnly);

    if (!lsblk.waitForFinished()) {
        return;
    }

    const QByteArray &diskStatusJson = lsblk.readAllStandardOutput();
    QJsonDocument diskStatus = QJsonDocument::fromJson(diskStatusJson);
    QJsonValue diskStatusJsonValue = diskStatus.object().value("blockdevices");
    QMap<QString, QPair<QString, qulonglong>> deviceParentAndSizeMap;

    if (!diskStatusJsonValue.isUndefined()) {
        QJsonArray diskStatusArray = diskStatusJsonValue.toArray();
        QString keyName;

        for (const QJsonValue &oneValue : diskStatusArray) {
            QString name = oneValue.toObject().value("name").toString();
            QString kname = oneValue.toObject().value("kname").toString();
            QString pkname = oneValue.toObject().value("pkname").toString();
            qulonglong size = oneValue.toObject().value("size").toVariant().toULongLong();

            if (keyName.isNull() && deviceName == name) {
                keyName = kname;
            }

            deviceParentAndSizeMap[kname] = QPair<QString, qulonglong>(pkname, size);
        }

        while (!deviceParentAndSizeMap[keyName].first.isNull()) {
            keyName = deviceParentAndSizeMap[keyName].first;
        }

        diskSize = deviceParentAndSizeMap[keyName].second;
    }
#endif
}

Q_GLOBAL_STATIC(DSysInfoPrivate, siGlobal)

QString DSysInfo::operatingSystemName()
{
    siGlobal->ensureReleaseInfo();

    return siGlobal->prettyName;
}

#ifdef Q_OS_LINUX
bool DSysInfo::isDeepin()
{
    siGlobal->ensureReleaseInfo();

    return productType() == Deepin;
}

bool DSysInfo::isDDE()
{
    siGlobal->ensureDeepinInfo();

    return siGlobal->deepinType != UnknownDeepin;
}

DSysInfo::DeepinType DSysInfo::deepinType()
{
    siGlobal->ensureDeepinInfo();

    return siGlobal->deepinType;
}

QString DSysInfo::deepinTypeDisplayName(const QLocale &locale)
{
    siGlobal->ensureDeepinInfo();

    return siGlobal->deepinTypeMap.value(locale.name(), siGlobal->deepinTypeMap.value(QString()));
}

QString DSysInfo::deepinVersion()
{
    siGlobal->ensureDeepinInfo();

    return siGlobal->deepinVersion;
}

QString DSysInfo::deepinEdition()
{
    siGlobal->ensureDeepinInfo();

    return siGlobal->deepinEdition;
}

QString DSysInfo::deepinCopyright()
{
    siGlobal->ensureDeepinInfo();

    return siGlobal->deepinCopyright;
}
#endif

DSysInfo::ProductType DSysInfo::productType()
{
    siGlobal->ensureReleaseInfo();

    return siGlobal->productType;
}

QString DSysInfo::productTypeString()
{
    siGlobal->ensureReleaseInfo();

    return siGlobal->productTypeString;
}

QString DSysInfo::productVersion()
{
    siGlobal->ensureReleaseInfo();

    return siGlobal->productVersion;
}

QString DSysInfo::computerName()
{
    siGlobal->ensureComputerInfo();

    return siGlobal->computerName;
}

QString DSysInfo::cpuModelName()
{
    siGlobal->ensureComputerInfo();

    return siGlobal->cpuModelName;
}

qint64 DSysInfo::memoryTotalSize()
{
    siGlobal->ensureComputerInfo();

    return siGlobal->memoryTotalSize;
}

qint64 DSysInfo::systemDiskSize()
{
    siGlobal->ensureComputerInfo();

    return siGlobal->diskSize;
}

DCORE_END_NAMESPACE
