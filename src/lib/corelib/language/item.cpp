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

#include "item.h"
#include "itempool.h"
#include "filecontext.h"
#include <logging/translator.h>
#include <tools/error.h>
#include <tools/qbsassert.h>

namespace qbs {
namespace Internal {

Item::Item(ItemPool *pool)
    : m_pool(pool)
    , m_propertyObserver(0)
    , m_moduleInstance(false)
    , m_prototype(0)
    , m_scope(0)
    , m_outerItem(0)
    , m_parent(0)
{
}

Item::~Item()
{
    if (m_propertyObserver)
        m_propertyObserver->onItemDestroyed(this);
}

Item *Item::create(ItemPool *pool)
{
    return pool->allocateItem();
}

Item *Item::clone(ItemPool *pool) const
{
    Item *dup = create(pool);
    dup->m_id = m_id;
    dup->m_typeName = m_typeName;
    dup->m_location = m_location;
    dup->m_moduleInstance = m_moduleInstance;
    dup->m_prototype = m_prototype;
    dup->m_scope = m_scope;
    dup->m_outerItem = m_outerItem;
    dup->m_parent = m_parent;
    dup->m_file = m_file;
    dup->m_propertyDeclarations = m_propertyDeclarations;
    dup->m_functions = m_functions;
    dup->m_modules = m_modules;

    dup->m_children.reserve(m_children.count());
    foreach (const Item *child, m_children) {
        Item *clonedChild = child->clone(pool);
        clonedChild->m_parent = dup;
        dup->m_children.append(clonedChild);
    }

    for (PropertyMap::const_iterator it = m_properties.constBegin(); it != m_properties.constEnd();
         ++it) {
        dup->m_properties.insert(it.key(), it.value()->clone());
    }

    return dup;
}

bool Item::hasProperty(const QString &name) const
{
    for (const Item *item = this; item; item = item->m_prototype)
        if (item->m_properties.contains(name))
            return true;

    return false;
}

bool Item::hasOwnProperty(const QString &name) const
{
    return m_properties.contains(name);
}

ValuePtr Item::property(const QString &name) const
{
    ValuePtr value;
    for (const Item *item = this; item; item = item->m_prototype)
        if ((value = item->m_properties.value(name)))
            break;
    return value;
}

ItemValuePtr Item::itemProperty(const QString &name, bool create)
{
    ItemValuePtr result;
    ValuePtr v = property(name);
    if (v && v->type() == Value::ItemValueType) {
        result = v.staticCast<ItemValue>();
    } else if (create) {
        result = ItemValue::create(Item::create(m_pool));
        setProperty(name, result);
    }
    return result;
}

JSSourceValuePtr Item::sourceProperty(const QString &name) const
{
    ValuePtr v = property(name);
    if (!v || v->type() != Value::JSSourceValueType)
        return JSSourceValuePtr();
    return v.staticCast<JSSourceValue>();
}

VariantValuePtr Item::variantProperty(const QString &name) const
{
    ValuePtr v = property(name);
    if (!v || v->type() != Value::VariantValueType)
        return VariantValuePtr();
    return v.staticCast<VariantValue>();
}

const PropertyDeclaration Item::propertyDeclaration(const QString &name) const
{
    const PropertyDeclaration decl = m_propertyDeclarations.value(name);
    return (!decl.isValid() && m_prototype) ? m_prototype->propertyDeclaration(name) : decl;
}

void Item::setPropertyObserver(ItemObserver *observer) const
{
    QBS_ASSERT(!observer || !m_propertyObserver, return);   // warn if accidentally overwritten
    m_propertyObserver = observer;
}

Item *Item::child(const QString &type, bool checkForMultiple) const
{
    Item *child = 0;
    foreach (Item * const currentChild, children()) {
        if (currentChild->typeName() == type) {
            if (!checkForMultiple)
                return currentChild;
            if (child) {
                ErrorInfo error(Tr::tr("Multiple instances of item '%1' found where at most one "
                                       "is allowed.").arg(type));
                error.append(Tr::tr("First item"), child->location());
                error.append(Tr::tr("Second item"), currentChild->location());
                throw error;
            }
            child = currentChild;
        }
    }
    return child;
}

} // namespace Internal
} // namespace qbs
