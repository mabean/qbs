/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qbs.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "language.h"

#include "artifactproperties.h"
#include "builtindeclarations.h"
#include "propertymapinternal.h"
#include "scriptengine.h"

#include <buildgraph/artifact.h>
#include <buildgraph/buildgraph.h>
#include <buildgraph/productbuilddata.h>
#include <buildgraph/projectbuilddata.h>
#include <buildgraph/rulegraph.h> // TODO: Move to language?
#include <buildgraph/transformer.h>
#include <jsextensions/jsextensions.h>
#include <logging/categories.h>
#include <logging/translator.h>
#include <tools/buildgraphlocker.h>
#include <tools/hostosinfo.h>
#include <tools/error.h>
#include <tools/fileinfo.h>
#include <tools/scripttools.h>
#include <tools/qbsassert.h>
#include <tools/qttools.h>
#include <tools/stringconstants.h>

#include <QtCore/qcryptographichash.h>
#include <QtCore/qdir.h>
#include <QtCore/qdiriterator.h>
#include <QtCore/qmap.h>

#include <QtScript/qscriptvalue.h>

#include <algorithm>
#include <memory>
#include <mutex>

namespace qbs {
namespace Internal {

template<typename T> bool equals(const T *v1, const T *v2)
{
    if (v1 == v2)
        return true;
    if (!v1 != !v2)
        return false;
    return *v1 == *v2;
}


/*!
 * \class FileTagger
 * \brief The \c FileTagger class maps 1:1 to the respective item in a qbs source file.
 */

FileTagger::FileTagger(const QStringList &patterns, const FileTags &fileTags, int priority)
    : m_fileTags(fileTags), m_priority(priority)
{
    setPatterns(patterns);
}

void FileTagger::setPatterns(const QStringList &patterns)
{
    m_patterns.clear();
    for (const QString &pattern : patterns) {
        QBS_CHECK(!pattern.isEmpty());
        m_patterns << QRegExp(pattern, Qt::CaseSensitive, QRegExp::Wildcard);
    }
}


bool Probe::needsReconfigure(const FileTime &referenceTime) const
{
    const auto criterion = [referenceTime](const QString &filePath) {
        FileInfo fi(filePath);
        return !fi.exists() || fi.lastModified() > referenceTime;
    };
    return std::any_of(m_importedFilesUsed.cbegin(), m_importedFilesUsed.cend(), criterion);
}


/*!
 * \class SourceArtifact
 * \brief The \c SourceArtifact class represents a source file.
 * Everything except the file path is inherited from the surrounding \c ResolvedGroup.
 * (TODO: Not quite true. Artifacts in transformers will be generated by the transformer, but are
 * still represented as source artifacts. We may or may not want to change this; if we do,
 * SourceArtifact could simply have a back pointer to the group in addition to the file path.)
 * \sa ResolvedGroup
 */


/*!
 * \class ResolvedGroup
 * \brief The \c ResolvedGroup class corresponds to the Group item in a qbs source file.
 */

 /*!
  * \variable ResolvedGroup::files
  * \brief The files listed in the group item's "files" binding.
  * Note that these do not include expanded wildcards.
  */

/*!
 * \variable ResolvedGroup::wildcards
 * \brief Represents the wildcard elements in this group's "files" binding.
 *  If no wildcards are specified there, this variable is null.
 * \sa SourceWildCards
 */

/*!
 * \brief Returns all files specified in the group item as source artifacts.
 * This includes the expanded list of wildcards.
 */
QList<SourceArtifactPtr> ResolvedGroup::allFiles() const
{
    QList<SourceArtifactPtr> lst = files;
    if (wildcards)
        lst << wildcards->files;
    return lst;
}

void ResolvedGroup::load(PersistentPool &pool)
{
    serializationOp<PersistentPool::Load>(pool);
    if (wildcards)
        wildcards->group = this;
}

void ResolvedGroup::store(PersistentPool &pool)
{
    serializationOp<PersistentPool::Store>(pool);
}

/*!
 * \class RuleArtifact
 * \brief The \c RuleArtifact class represents an Artifact item encountered in the context
 *        of a Rule item.
 * When applying the rule, one \c Artifact object will be constructed from each \c RuleArtifact
 * object. During that process, the \c RuleArtifact's bindings are evaluated and the results
 * are inserted into the corresponding \c Artifact's properties.
 * \sa Rule
 */


/*!
 * \class ScriptFunction
 * \brief The \c ScriptFunction class represents the JavaScript code found in the "prepare" binding
 *        of a \c Rule item in a qbs file.
 * \sa Rule
 */

ScriptFunction::ScriptFunction()
{

}

ScriptFunction::~ScriptFunction()
{

}

 /*!
  * \variable ScriptFunction::script
  * \brief The actual Javascript code, taken verbatim from the qbs source file.
  */

  /*!
   * \variable ScriptFunction::location
   * \brief The exact location of the script in the qbs source file.
   * This is mostly needed for diagnostics.
   */

bool ScriptFunction::isValid() const
{
    return location.line() != -1;
}

bool operator==(const ScriptFunction &a, const ScriptFunction &b)
{
    return a.sourceCode == b.sourceCode
            && a.location == b.location
            && equals(a.fileContext.get(), b.fileContext.get());
}

QStringList ResolvedModule::argumentNamesForSetupBuildEnv()
{
    static const QStringList argNames = BuiltinDeclarations::instance()
            .argumentNamesForScriptFunction(ItemType::Module,
                                            StringConstants::setupBuildEnvironmentProperty());
    return argNames;
}

QStringList ResolvedModule::argumentNamesForSetupRunEnv()
{
    static const QStringList argNames = BuiltinDeclarations::instance()
            .argumentNamesForScriptFunction(ItemType::Module,
                                            StringConstants::setupRunEnvironmentProperty());
    return argNames;
}

bool operator==(const ResolvedModule &m1, const ResolvedModule &m2)
{
    return m1.name == m2.name
            && m1.isProduct == m2.isProduct
            && m1.moduleDependencies.toSet() == m2.moduleDependencies.toSet()
            && m1.setupBuildEnvironmentScript == m2.setupBuildEnvironmentScript
            && m1.setupRunEnvironmentScript == m2.setupRunEnvironmentScript;
}

RulePtr Rule::clone() const
{
    return std::make_shared<Rule>(*this);
}

QStringList Rule::argumentNamesForOutputArtifacts()
{
    static const QStringList argNames = BuiltinDeclarations::instance()
            .argumentNamesForScriptFunction(ItemType::Rule,
                                            StringConstants::outputArtifactsProperty());
    return argNames;
}

QStringList Rule::argumentNamesForPrepare()
{
    static const QStringList argNames = BuiltinDeclarations::instance()
            .argumentNamesForScriptFunction(ItemType::Rule, StringConstants::prepareProperty());
    return argNames;
}

QString Rule::toString() const
{
    QStringList outputTagsSorted = collectedOutputFileTags().toStringList();
    outputTagsSorted.sort();
    FileTags inputTags = inputs;
    inputTags.unite(inputsFromDependencies);
    QStringList inputTagsSorted = inputTags.toStringList();
    inputTagsSorted.sort();
    return QLatin1Char('[') + outputTagsSorted.join(QLatin1Char(','))
            + QLatin1String("][")
            + inputTagsSorted.join(QLatin1Char(',')) + QLatin1Char(']');
}

bool Rule::acceptsAsInput(Artifact *artifact) const
{
    return artifact->fileTags().intersects(inputs);
}

FileTags Rule::staticOutputFileTags() const
{
    FileTags result;
    for (const RuleArtifactConstPtr &artifact : qAsConst(artifacts))
        result.unite(artifact->fileTags);
    return result;
}

FileTags Rule::collectedOutputFileTags() const
{
    FileTags result = outputFileTags.empty() ? staticOutputFileTags() : outputFileTags;
    for (const auto &ap : qAsConst(product->artifactProperties)) {
        if (ap->fileTagsFilter().intersects(result))
            result += ap->extraFileTags();
    }
    return result;
}

bool Rule::isDynamic() const
{
    return outputArtifactsScript.isValid();
}

bool Rule::declaresInputs() const
{
    return !inputs.empty() || !inputsFromDependencies.empty();
}

ResolvedProduct::ResolvedProduct()
    : enabled(true)
{
}

ResolvedProduct::~ResolvedProduct()
{
}

void ResolvedProduct::accept(BuildGraphVisitor *visitor) const
{
    if (!buildData)
        return;
    for (BuildGraphNode * const node : qAsConst(buildData->rootNodes()))
        node->accept(visitor);
}

/*!
 * \brief Returns all files of all groups as source artifacts.
 * This includes the expanded list of wildcards.
 */
QList<SourceArtifactPtr> ResolvedProduct::allFiles() const
{
    QList<SourceArtifactPtr> lst;
    for (const GroupConstPtr &group : groups)
        lst << group->allFiles();
    return lst;
}

/*!
 * \brief Returns all files of all enabled groups as source artifacts.
 * \sa ResolvedProduct::allFiles()
 */
QList<SourceArtifactPtr> ResolvedProduct::allEnabledFiles() const
{
    QList<SourceArtifactPtr> lst;
    for (const GroupConstPtr &group : groups) {
        if (group->enabled)
            lst << group->allFiles();
    }
    return lst;
}

FileTags ResolvedProduct::fileTagsForFileName(const QString &fileName) const
{
    FileTags result;
    std::unique_ptr<int> priority;
    for (const FileTaggerConstPtr &tagger : qAsConst(fileTaggers)) {
        for (const QRegExp &pattern : tagger->patterns()) {
            if (FileInfo::globMatches(pattern, fileName)) {
                if (priority) {
                    if (*priority != tagger->priority()) {
                        // The taggers are expected to be sorted by priority.
                        QBS_ASSERT(*priority > tagger->priority(), return result);
                        return result;
                    }
                } else {
                    priority.reset(new int(tagger->priority()));
                }
                result.unite(tagger->fileTags());
                break;
            }
        }
    }
    return result;
}

void ResolvedProduct::load(PersistentPool &pool)
{
    serializationOp<PersistentPool::Load>(pool);
    for (const RulePtr &rule : rules)
        rule->product = this;
    for (const ResolvedModulePtr &module : modules)
        module->product = this;
}

void ResolvedProduct::store(PersistentPool &pool)
{
    serializationOp<PersistentPool::Store>(pool);
}

void ResolvedProduct::registerArtifactWithChangedInputs(Artifact *artifact)
{
    QBS_CHECK(buildData);
    QBS_CHECK(artifact->product == this);
    QBS_CHECK(artifact->transformer);
    if (artifact->transformer->rule->multiplex) {
        // Reapplication of rules only makes sense for multiplex rules (e.g. linker).
        buildData->addArtifactWithChangedInputsForRule(artifact->transformer->rule, artifact);
    }
}

void ResolvedProduct::unregisterArtifactWithChangedInputs(Artifact *artifact)
{
    QBS_CHECK(buildData);
    QBS_CHECK(artifact->product == this);
    QBS_CHECK(artifact->transformer);
    buildData->removeArtifactWithChangedInputsForRule(artifact->transformer->rule, artifact);
}

void ResolvedProduct::unmarkForReapplication(const RuleConstPtr &rule)
{
    QBS_CHECK(buildData);
    buildData->removeAllArtifactsWithChangedInputsForRule(rule);
}

bool ResolvedProduct::isMarkedForReapplication(const RuleConstPtr &rule) const
{
    return buildData->ruleHasArtifactWithChangedInputs(rule);
}

ArtifactSet ResolvedProduct::lookupArtifactsByFileTag(const FileTag &tag) const
{
    QBS_CHECK(buildData);
    return buildData->artifactsByFileTag().value(tag);
}

ArtifactSet ResolvedProduct::lookupArtifactsByFileTags(const FileTags &tags) const
{
    QBS_CHECK(buildData);
    ArtifactSet set;
    for (const FileTag &tag : tags)
        set = set.unite(buildData->artifactsByFileTag().value(tag));
    return set;
}

ArtifactSet ResolvedProduct::targetArtifacts() const
{
    QBS_CHECK(buildData);
    ArtifactSet taSet;
    for (Artifact * const a : buildData->rootArtifacts()) {
        if (a->fileTags().intersects(fileTags))
            taSet << a;
    }
    return taSet;
}

TopLevelProject *ResolvedProduct::topLevelProject() const
{
    return project->topLevelProject();
}

QString ResolvedProduct::uniqueName(const QString &name, const QString &multiplexConfigurationId)
{
    QString result = name;
    if (!multiplexConfigurationId.isEmpty())
        result.append(QLatin1Char('.')).append(multiplexConfigurationId);
    return result;
}

QString ResolvedProduct::uniqueName() const
{
    return uniqueName(name, multiplexConfigurationId);
}

QString ResolvedProduct::fullDisplayName(const QString &name,
                                         const QString &multiplexConfigurationId)
{
    QString result = name;
    if (!multiplexConfigurationId.isEmpty())
        result.append(QLatin1Char(' ')).append(multiplexIdToString(multiplexConfigurationId));
    return result;
}

QString ResolvedProduct::fullDisplayName() const
{
    return fullDisplayName(name, multiplexConfigurationId);
}

QString ResolvedProduct::profile() const
{
    return moduleProperties->qbsPropertyValue(StringConstants::profileProperty()).toString();
}

static QStringList findGeneratedFiles(const Artifact *base, bool recursive, const FileTags &tags)
{
    QStringList result;
    for (const Artifact *parent : base->parentArtifacts()) {
        if (tags.empty() || parent->fileTags().intersects(tags))
            result << parent->filePath();
        if (recursive)
            result << findGeneratedFiles(parent, true, tags);
    }
    return result;
}

QStringList ResolvedProduct::generatedFiles(const QString &baseFile, bool recursive,
                                            const FileTags &tags) const
{
    ProductBuildData *data = buildData.get();
    if (!data)
        return QStringList();

    for (const Artifact *art : filterByType<Artifact>(data->allNodes())) {
        if (art->filePath() == baseFile)
            return findGeneratedFiles(art, recursive, tags);
    }
    return QStringList();
}

QString ResolvedProduct::deriveBuildDirectoryName(const QString &name,
                                                  const QString &multiplexConfigurationId)
{
    QString dirName = uniqueName(name, multiplexConfigurationId);
    const QByteArray hash = QCryptographicHash::hash(dirName.toUtf8(), QCryptographicHash::Sha1);
    return HostOsInfo::rfc1034Identifier(dirName)
            .append(QLatin1Char('.'))
            .append(QString::fromLatin1(hash.toHex().left(8)));
}

QString ResolvedProduct::buildDirectory() const
{
    return productProperties.value(StringConstants::buildDirectoryProperty()).toString();
}

bool ResolvedProduct::isInParentProject(const ResolvedProductConstPtr &other) const
{
    for (const ResolvedProject *otherParent = other->project.get(); otherParent;
         otherParent = otherParent->parentProject.get()) {
        if (otherParent == project.get())
            return true;
    }
    return false;
}

bool ResolvedProduct::builtByDefault() const
{
    return productProperties.value(StringConstants::builtByDefaultProperty(), true).toBool();
}

void ResolvedProduct::cacheExecutablePath(const QString &origFilePath, const QString &fullFilePath)
{
    std::lock_guard<std::mutex> locker(m_executablePathCacheLock);
    m_executablePathCache.insert(origFilePath, fullFilePath);
}

QString ResolvedProduct::cachedExecutablePath(const QString &origFilePath) const
{
    std::lock_guard<std::mutex> locker(m_executablePathCacheLock);
    return m_executablePathCache.value(origFilePath);
}


ResolvedProject::ResolvedProject() : enabled(true), m_topLevelProject(nullptr)
{
}

ResolvedProject::~ResolvedProject()
{
}

void ResolvedProject::accept(BuildGraphVisitor *visitor) const
{
    for (const ResolvedProductPtr &product : qAsConst(products))
        product->accept(visitor);
    for (const ResolvedProjectPtr &subProject : qAsConst(subProjects))
        subProject->accept(visitor);
}

TopLevelProject *ResolvedProject::topLevelProject()
{
    if (m_topLevelProject)
        return m_topLevelProject;
    if (parentProject.expired()) {
        m_topLevelProject = static_cast<TopLevelProject *>(this);
        return m_topLevelProject;
    }
    m_topLevelProject = parentProject->topLevelProject();
    return m_topLevelProject;
}

QList<ResolvedProjectPtr> ResolvedProject::allSubProjects() const
{
    QList<ResolvedProjectPtr> projectList = subProjects;
    for (const ResolvedProjectConstPtr &subProject : qAsConst(subProjects))
        projectList << subProject->allSubProjects();
    return projectList;
}

QList<ResolvedProductPtr> ResolvedProject::allProducts() const
{
    QList<ResolvedProductPtr> productList = products;
    for (const ResolvedProjectConstPtr &subProject : qAsConst(subProjects))
        productList << subProject->allProducts();
    return productList;
}

void ResolvedProject::load(PersistentPool &pool)
{
    serializationOp<PersistentPool::Load>(pool);
    std::for_each(products.cbegin(), products.cend(),
                  [](const ResolvedProductPtr &p) {
        if (!p->buildData)
            return;
        for (BuildGraphNode * const node : qAsConst(p->buildData->allNodes())) {
            node->product = p;

            // restore parent links
            for (BuildGraphNode * const child : qAsConst(node->children))
                child->parents.insert(node);
        }
    });
}

void ResolvedProject::store(PersistentPool &pool)
{
    serializationOp<PersistentPool::Store>(pool);
}


TopLevelProject::TopLevelProject()
    : bgLocker(nullptr), locked(false), lastResolveTime(FileTime::oldestTime())
{
}

TopLevelProject::~TopLevelProject()
{
    delete bgLocker;
}

QString TopLevelProject::deriveId(const QVariantMap &config)
{
    const QVariantMap qbsProperties = config.value(StringConstants::qbsModule()).toMap();
    const QString configurationName = qbsProperties.value(
                StringConstants::configurationNameProperty()).toString();
    return configurationName;
}

QString TopLevelProject::deriveBuildDirectory(const QString &buildRoot, const QString &id)
{
    return buildRoot + QLatin1Char('/') + id;
}

void TopLevelProject::setBuildConfiguration(const QVariantMap &config)
{
    m_buildConfiguration = config;
    m_id = deriveId(config);
}

QString TopLevelProject::profile() const
{
    return projectProperties().value(StringConstants::profileProperty()).toString();
}

QString TopLevelProject::buildGraphFilePath() const
{
    return ProjectBuildData::deriveBuildGraphFilePath(buildDirectory, id());
}

void TopLevelProject::store(Logger logger)
{
    // TODO: Use progress observer here.

    if (!buildData)
        return;
    if (!buildData->isDirty()) {
        qCDebug(lcBuildGraph) << "build graph is unchanged in project" << id();
        return;
    }
    const QString fileName = buildGraphFilePath();
    qCDebug(lcBuildGraph) << "storing:" << fileName;
    PersistentPool pool(logger);
    PersistentPool::HeadData headData;
    headData.projectConfig = buildConfiguration();
    pool.setHeadData(headData);
    pool.setupWriteStream(fileName);
    store(pool);
    pool.finalizeWriteStream();
    buildData->setClean();
}

void TopLevelProject::load(PersistentPool &pool)
{
    ResolvedProject::load(pool);
    serializationOp<PersistentPool::Load>(pool);
    QBS_CHECK(buildData);
}

void TopLevelProject::store(PersistentPool &pool)
{
    ResolvedProject::store(pool);
    serializationOp<PersistentPool::Store>(pool);
}

/*!
 * \class SourceWildCards
 * \brief Objects of the \c SourceWildCards class result from giving wildcards in a
 *        \c ResolvedGroup's "files" binding.
 * \sa ResolvedGroup
 */

/*!
  * \variable SourceWildCards::prefix
  * \brief Inherited from the \c ResolvedGroup
  * \sa ResolvedGroup
  */

/*!
 * \variable SourceWildCards::patterns
 * \brief All elements of the \c ResolvedGroup's "files" binding that contain wildcards.
 * \sa ResolvedGroup
 */

/*!
 * \variable SourceWildCards::excludePatterns
 * \brief Corresponds to the \c ResolvedGroup's "excludeFiles" binding.
 * \sa ResolvedGroup
 */

/*!
 * \variable SourceWildCards::files
 * \brief The \c SourceArtifacts resulting from the expanded list of matching files.
 */

Set<QString> SourceWildCards::expandPatterns(const GroupConstPtr &group,
                                              const QString &baseDir, const QString &buildDir)
{
    Set<QString> files = expandPatterns(group, patterns, baseDir, buildDir);
    files -= expandPatterns(group, excludePatterns, baseDir, buildDir);
    return files;
}

Set<QString> SourceWildCards::expandPatterns(const GroupConstPtr &group,
        const QStringList &patterns, const QString &baseDir, const QString &buildDir)
{
    Set<QString> files;
    QString expandedPrefix = group->prefix;
    if (expandedPrefix.startsWith(StringConstants::tildeSlash()))
        expandedPrefix.replace(0, 1, QDir::homePath());
    for (QString pattern : patterns) {
        pattern.prepend(expandedPrefix);
        pattern.replace(QLatin1Char('\\'), QLatin1Char('/'));
        QStringList parts = pattern.split(QLatin1Char('/'), QString::SkipEmptyParts);
        if (FileInfo::isAbsolute(pattern)) {
            QString rootDir;
            if (HostOsInfo::isWindowsHost() && pattern.at(0) != QLatin1Char('/')) {
                rootDir = parts.takeFirst();
                if (!rootDir.endsWith(QLatin1Char('/')))
                    rootDir.append(QLatin1Char('/'));
            } else {
                rootDir = QLatin1Char('/');
            }
            expandPatterns(files, group, parts, rootDir, buildDir);
        } else {
            expandPatterns(files, group, parts, baseDir, buildDir);
        }
    }

    return files;
}

void SourceWildCards::expandPatterns(Set<QString> &result, const GroupConstPtr &group,
                                     const QStringList &parts,
                                     const QString &baseDir, const QString &buildDir)
{
    // People might build directly in the project source directory. This is okay, since
    // we keep the build data in a "container" directory. However, we must make sure we don't
    // match any generated files therein as source files.
    if (baseDir.startsWith(buildDir))
        return;

    dirTimeStamps.push_back({ baseDir, FileInfo(baseDir).lastModified() });

    QStringList changed_parts = parts;
    bool recursive = false;
    QString part = changed_parts.takeFirst();

    while (part == QStringLiteral("**")) {
        recursive = true;

        if (changed_parts.empty()) {
            part = StringConstants::star();
            break;
        }

        part = changed_parts.takeFirst();
    }

    const bool isDir = !changed_parts.empty();

    const QString &filePattern = part;
    const QDirIterator::IteratorFlags itFlags = recursive
            ? QDirIterator::Subdirectories
            : QDirIterator::NoIteratorFlags;
    QDir::Filters itFilters = isDir
            ? QDir::Dirs
            : QDir::Files | QDir::System
              | QDir::Dirs; // This one is needed to get symbolic links to directories

    if (isDir && !FileInfo::isPattern(filePattern))
        itFilters |= QDir::Hidden;
    if (filePattern != StringConstants::dotDot() && filePattern != StringConstants::dot())
        itFilters |= QDir::NoDotAndDotDot;

    QDirIterator it(baseDir, QStringList(filePattern), itFilters, itFlags);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QString parentDir = it.fileInfo().dir().path();
        if (parentDir.startsWith(buildDir))
            continue; // See above.
        if (!isDir && it.fileInfo().isDir() && !it.fileInfo().isSymLink())
            continue;
        if (isDir) {
            expandPatterns(result, group, changed_parts, filePath, buildDir);
        } else {
            if (parentDir != baseDir)
                dirTimeStamps.push_back({parentDir, FileInfo(baseDir).lastModified()});
            result += QDir::cleanPath(filePath);
        }
    }
}

template<typename L>
QMap<QString, typename L::value_type> listToMap(const L &list)
{
    using V = typename L::value_type;
    QMap<QString, V> map;
    for (const V &elem : list)
        map.insert(keyFromElem(elem), elem);
    return map;
}

template<typename L>
bool listsAreEqual(const L &l1, const L &l2)
{
    if (l1.size() != l2.size())
        return false;
    using V = typename L::value_type;
    const QMap<QString, V> map1 = listToMap(l1);
    const QMap<QString, V> map2 = listToMap(l2);
    for (const QString &key : map1.keys()) {
        const V &value2 = map2.value(key);
        if (!value2)
            return false;
        if (!equals(map1.value(key).get(), value2.get()))
            return false;
    }
    return true;
}

QString keyFromElem(const SourceArtifactPtr &sa) { return sa->absoluteFilePath; }
QString keyFromElem(const RulePtr &r) {
    QString key = r->toString() + r->prepareScript.sourceCode();
    if (r->outputArtifactsScript.isValid())
        key += r->outputArtifactsScript.sourceCode();
    for (const auto &a : qAsConst(r->artifacts)) {
        key += a->filePath;
    }
    return key;
}

QString keyFromElem(const ArtifactPropertiesPtr &ap)
{
    QStringList lst = ap->fileTagsFilter().toStringList();
    lst.sort();
    return lst.join(QLatin1Char(','));
}

bool operator==(const SourceArtifactInternal &sa1, const SourceArtifactInternal &sa2)
{
    return sa1.absoluteFilePath == sa2.absoluteFilePath
            && sa1.fileTags == sa2.fileTags
            && sa1.overrideFileTags == sa2.overrideFileTags
            && sa1.targetOfModule == sa2.targetOfModule
            && *sa1.properties == *sa2.properties;
}

bool sourceArtifactSetsAreEqual(const QList<SourceArtifactPtr> &l1,
                                 const QList<SourceArtifactPtr> &l2)
{
    return listsAreEqual(l1, l2);
}

bool operator==(const Rule &r1, const Rule &r2)
{
    if (r1.artifacts.size() != r2.artifacts.size())
        return false;
    for (int i = 0; i < r1.artifacts.size(); ++i) {
        if (!equals(r1.artifacts.at(i).get(), r2.artifacts.at(i).get()))
            return false;
    }

    return r1.module->name == r2.module->name
            && r1.prepareScript == r2.prepareScript
            && r1.outputArtifactsScript == r2.outputArtifactsScript
            && r1.inputs == r2.inputs
            && r1.outputFileTags == r2.outputFileTags
            && r1.auxiliaryInputs == r2.auxiliaryInputs
            && r1.excludedInputs == r2.excludedInputs
            && r1.inputsFromDependencies == r2.inputsFromDependencies
            && r1.explicitlyDependsOn == r2.explicitlyDependsOn
            && r1.multiplex == r2.multiplex
            && r1.requiresInputs == r2.requiresInputs
            && r1.alwaysRun == r2.alwaysRun;
}

bool ruleListsAreEqual(const std::vector<RulePtr> &l1, const std::vector<RulePtr> &l2)
{
    return listsAreEqual(l1, l2);
}

bool operator==(const RuleArtifact &a1, const RuleArtifact &a2)
{
    return a1.filePath == a2.filePath
            && a1.fileTags == a2.fileTags
            && a1.alwaysUpdated == a2.alwaysUpdated
            && Set<RuleArtifact::Binding>::fromStdVector(a1.bindings) ==
               Set<RuleArtifact::Binding>::fromStdVector(a2.bindings);
}

bool operator==(const RuleArtifact::Binding &b1, const RuleArtifact::Binding &b2)
{
    return b1.code == b2.code && b1.name == b2.name;
}

uint qHash(const RuleArtifact::Binding &b)
{
    return qHash(std::make_pair(b.code, b.name.join(QLatin1Char(','))));
}

bool artifactPropertyListsAreEqual(const QList<ArtifactPropertiesPtr> &l1,
                                   const QList<ArtifactPropertiesPtr> &l2)
{
    return listsAreEqual(l1, l2);
}

QString multiplexIdToString(const QString &id)
{
    return QString::fromUtf8(QByteArray::fromBase64(id.toUtf8()));
}

bool operator==(const PrivateScriptFunction &a, const PrivateScriptFunction &b)
{
    return equals(a.m_sharedData.get(), b.m_sharedData.get());
}

bool operator==(const ExportedProperty &p1, const ExportedProperty &p2)
{
    return p1.fullName == p2.fullName
            && p1.type == p2.type
            && p1.sourceCode == p2.sourceCode
            && p1.isBuiltin == p2.isBuiltin;
}

bool operator==(const ExportedModuleDependency &d1, const ExportedModuleDependency &d2)
{
    return d1.name == d2.name && d1.moduleProperties == d2.moduleProperties;
}

bool equals(const std::vector<ExportedItemPtr> &l1, const std::vector<ExportedItemPtr> &l2)
{
    static const auto cmp = [](const ExportedItemPtr &p1, const ExportedItemPtr &p2) {
        return *p1 == *p2;
    };
    return l1.size() == l2.size() && std::equal(l1.cbegin(), l1.cend(), l2.cbegin(), cmp);
}

bool operator==(const ExportedItem &i1, const ExportedItem &i2)
{
    return i1.name == i2.name
            && i1.properties == i2.properties
            && equals(i1.children, i2.children);
}

bool operator==(const ExportedModule &m1, const ExportedModule &m2)
{
    static const auto cmpProductsByName = [](
            const ResolvedProductConstPtr &p1,
            const ResolvedProductConstPtr &p2) {
        return p1->name == p2->name;
    };
    static const auto depMapsEqual = [](const QMap<ResolvedProductConstPtr, QVariantMap> &m1,
            const QMap<ResolvedProductConstPtr, QVariantMap> &m2) {
        if (m1.size() != m2.size())
            return false;
        for (auto it1 = m1.cbegin(), it2 = m2.cbegin(); it1 != m1.cend(); ++it1, ++it2) {
            if (it1.key()->name != it2.key()->name)
                return false;
            if (it1.value() != it2.value())
                return false;
        }
        return true;
    };

    return m1.propertyValues == m2.propertyValues
            && m1.modulePropertyValues == m2.modulePropertyValues
            && equals(m1.children, m2.children)
            && m1.m_properties == m2.m_properties
            && m1.importStatements == m2.importStatements
            && m1.productDependencies.size() == m2.productDependencies.size()
            && std::equal(m1.productDependencies.cbegin(), m1.productDependencies.cend(),
                          m2.productDependencies.cbegin(), cmpProductsByName)
            && depMapsEqual(m1.dependencyParameters, m2.dependencyParameters);
}

} // namespace Internal
} // namespace qbs
