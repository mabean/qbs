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
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/
#include "codelocation.h"

#include <tools/fileinfo.h>
#include <tools/qbsassert.h>

#include <QDataStream>
#include <QDir>
#include <QRegExp>
#include <QSharedData>
#include <QString>

namespace qbs {

class CodeLocation::CodeLocationPrivate : public QSharedData
{
public:
    QString filePath;
    int line;
    int column;
};

CodeLocation::CodeLocation()
{
}

CodeLocation::CodeLocation(const QString &aFilePath, int aLine, int aColumn)
    : d(new CodeLocationPrivate)
{
    QBS_ASSERT(Internal::FileInfo::isAbsolute(aFilePath), qDebug() << aFilePath);
    d->filePath = aFilePath;
    d->line = aLine;
    d->column = aColumn;
}

CodeLocation::CodeLocation(const CodeLocation &other) : d(other.d)
{
}

CodeLocation &CodeLocation::operator=(const CodeLocation &other)
{
    d = other.d;
    return *this;
}

CodeLocation::~CodeLocation()
{
}

QString CodeLocation::filePath() const
{
    return d ? d->filePath : QString();
}

int CodeLocation::line() const
{
    return d ? d->line : -1;
}

int CodeLocation::column() const
{
    return d ? d->column : -1;
}

bool CodeLocation::isValid() const
{
    return !filePath().isEmpty();
}

QString CodeLocation::toString() const
{
    QString str;
    if (isValid()) {
        str = QDir::toNativeSeparators(filePath());
        QString lineAndColumn;
        if (line() > 0 && !str.contains(QRegExp(QLatin1String(":[0-9]+$"))))
            lineAndColumn += QLatin1Char(':') + QString::number(line());
        if (column() > 0 && !str.contains(QRegExp(QLatin1String(":[0-9]+:[0-9]+$"))))
            lineAndColumn += QLatin1Char(':') + QString::number(column());
        str += lineAndColumn;
    }
    return str;
}

bool operator==(const CodeLocation &cl1, const CodeLocation &cl2)
{
    if (cl1.d == cl2.d)
        return true;
    return cl1.filePath() == cl2.filePath() && cl1.line() == cl2.line()
            && cl1.column() == cl2.column();
}

bool operator!=(const CodeLocation &cl1, const CodeLocation &cl2)
{
    return !(cl1 == cl2);
}

QDataStream &operator<<(QDataStream &s, const CodeLocation &o)
{
    s << o.filePath();
    s << o.line();
    s << o.column();
    return s;
}

QDataStream &operator>>(QDataStream &s, CodeLocation &o)
{
    QString filePath;
    int line;
    int column;
    s >> filePath;
    s >> line;
    s >> column;
    o = CodeLocation(filePath, line, column);
    return s;
}

} // namespace qbs
