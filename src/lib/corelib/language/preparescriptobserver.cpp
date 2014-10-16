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

#include "preparescriptobserver.h"

#include "property.h"
#include "scriptengine.h"

#include <QScriptValue>

namespace qbs {
namespace Internal {

PrepareScriptObserver::PrepareScriptObserver(ScriptEngine *engine)
    : m_engine(engine)
    , m_productObjectId(-1)
    , m_projectObjectId(-1)
{
}

void PrepareScriptObserver::onPropertyRead(const QScriptValue &object, const QString &name,
                                           const QScriptValue &value)
{
    if (object.objectId() == m_productObjectId) {
        m_engine->addPropertyRequestedInScript(
                    Property(QString(), name, value.toVariant(), Property::PropertyInProduct));
    } else if (object.objectId() == m_projectObjectId) {
        m_engine->addPropertyRequestedInScript(
                    Property(QString(), name, value.toVariant(), Property::PropertyInProject));
    }

}


} // namespace Internal
} // namespace qbs
