/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the Qt Build Suite.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://www.qt.io/licensing.  For further information
** use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file.  Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#ifndef QBS_HOSTOSINFO_H
#define QBS_HOSTOSINFO_H

#include "qbs_export.h"

#include <QtGlobal>
#include <QMap>
#include <QString>
#include <QStringList>

#if defined(Q_OS_WIN)
#define QBS_HOST_EXE_SUFFIX ".exe"
#define QBS_HOST_DYNAMICLIB_PREFIX ""
#define QBS_HOST_DYNAMICLIB_SUFFIX ".dll"
#elif defined(Q_OS_DARWIN)
#define QBS_HOST_EXE_SUFFIX ""
#define QBS_HOST_DYNAMICLIB_PREFIX "lib"
#define QBS_HOST_DYNAMICLIB_SUFFIX ".dylib"
#else
#define QBS_HOST_EXE_SUFFIX ""
#define QBS_HOST_DYNAMICLIB_PREFIX "lib"
#define QBS_HOST_DYNAMICLIB_SUFFIX ".so"
#endif // Q_OS_WIN

namespace qbs {
namespace Internal {

class QBS_EXPORT HostOsInfo // Exported for use by command-line tools.
{
public:
    // Add more as needed.
    enum HostOs { HostOsWindows, HostOsLinux, HostOsOsx, HostOsOtherUnix, HostOsOther };

    static inline HostOs hostOs();

    static bool isWindowsHost() { return hostOs() == HostOsWindows; }
    static bool isLinuxHost() { return hostOs() == HostOsLinux; }
    static bool isOsxHost() { return hostOs() == HostOsOsx; }
    static inline bool isAnyUnixHost();

    static QString appendExecutableSuffix(const QString &executable)
    {
        QString finalName = executable;
        if (isWindowsHost())
            finalName += QLatin1String(QBS_HOST_EXE_SUFFIX);
        return finalName;
    }

    static QString dynamicLibraryName(const QString &libraryBaseName)
    {
        return QLatin1String(QBS_HOST_DYNAMICLIB_PREFIX) + libraryBaseName
                + QLatin1String(QBS_HOST_DYNAMICLIB_SUFFIX);
    }

    static Qt::CaseSensitivity fileNameCaseSensitivity()
    {
        return isWindowsHost() ? Qt::CaseInsensitive: Qt::CaseSensitive;
    }

    static QChar pathListSeparator()
    {
        return isWindowsHost() ? QLatin1Char(';') : QLatin1Char(':');
    }

    static Qt::KeyboardModifier controlModifier()
    {
        return isOsxHost() ? Qt::MetaModifier : Qt::ControlModifier;
    }
};

HostOsInfo::HostOs HostOsInfo::hostOs()
{
#if defined(Q_OS_WIN)
    return HostOsWindows;
#elif defined(Q_OS_LINUX)
    return HostOsLinux;
#elif defined(Q_OS_DARWIN)
    return HostOsOsx;
#elif defined(Q_OS_UNIX)
    return HostOsOtherUnix;
#else
    return HostOsOther;
#endif
}

bool HostOsInfo::isAnyUnixHost()
{
#ifdef Q_OS_UNIX
    return true;
#else
    return false;
#endif
}

} // namespace Internal
} // namespace qbs

#endif // QBS_HOSTOSINFO_H
