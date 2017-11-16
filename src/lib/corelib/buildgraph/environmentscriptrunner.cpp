/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
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

#include "environmentscriptrunner.h"

#include "buildgraph.h"
#include "rulesevaluationcontext.h"
#include <language/language.h>
#include <language/propertymapinternal.h>
#include <language/resolvedfilecontext.h>
#include <language/scriptengine.h>
#include <tools/qbsassert.h>
#include <tools/qttools.h>
#include <logging/translator.h>

#include <QtCore/qhash.h>
#include <QtCore/qvariant.h>

#include <algorithm>

namespace qbs {
namespace Internal {

class EnvProvider
{
public:
    EnvProvider(ScriptEngine *engine, const QProcessEnvironment &originalEnv)
        : m_engine(engine), m_env(originalEnv)
    {
        QVariant v;
        v.setValue<void*>(&m_env);
        m_engine->setProperty(m_procenvProperty, v);
    }

    ~EnvProvider() { m_engine->setProperty(m_procenvProperty, QVariant()); }

    QProcessEnvironment alteredEnvironment() const { return m_env; }

private:
    ScriptEngine * const m_engine;
    QProcessEnvironment m_env;
    static const char * const m_procenvProperty;
};

// TODO: Define this at a central place.
const char * const EnvProvider::m_procenvProperty = "_qbs_procenv";

static QList<const ResolvedModule*> topSortModules(const QHash<const ResolvedModule*,
                                                   QList<const ResolvedModule*> > &moduleChildren,
                                                   const QList<const ResolvedModule*> &modules,
                                                   Set<QString> &seenModuleNames)
{
    QList<const ResolvedModule*> result;
    for (const ResolvedModule * const m : modules) {
        if (m->name.isNull())
            continue;
        result.append(topSortModules(moduleChildren, moduleChildren.value(m), seenModuleNames));
        if (seenModuleNames.insert(m->name).second)
            result.append(m);
    }
    return result;
}

EnvironmentScriptRunner::EnvironmentScriptRunner(ResolvedProduct *product,
                                                 RulesEvaluationContext *evalContext,
                                                 const QProcessEnvironment &env)
    : m_product(product), m_evalContext(evalContext), m_env(env)
{
}

void EnvironmentScriptRunner::setupForBuild()
{
    // TODO: Won't this fail to take changed properties into account? We probably need
    //       change tracking here as well.
    if (!m_product->buildEnvironment.isEmpty())
        return;
    m_envType = BuildEnv;
    setupEnvironment();
}

void EnvironmentScriptRunner::setupForRun(const QStringList &config)
{
    m_envType = RunEnv;
    m_runEnvConfig = config;
    setupEnvironment();
}

void EnvironmentScriptRunner::setupEnvironment()
{
    const auto hasScript = [this](const ResolvedModuleConstPtr &m) {
        return !getScript(m.get())->sourceCode.isEmpty();
    };
    const bool hasAnyScripts = std::any_of(m_product->modules.cbegin(), m_product->modules.cend(),
                                           hasScript);
    if (!hasAnyScripts)
        return;

    QMap<QString, const ResolvedModule *> moduleMap;
    for (const ResolvedModuleConstPtr &module : m_product->modules)
        moduleMap.insert(module->name, module.get());

    QHash<const ResolvedModule*, QList<const ResolvedModule*> > moduleParents;
    QHash<const ResolvedModule*, QList<const ResolvedModule*> > moduleChildren;
    for (const ResolvedModuleConstPtr &module : m_product->modules) {
        for (const QString &moduleName : qAsConst(module->moduleDependencies)) {
            const ResolvedModule * const depmod = moduleMap.value(moduleName);
            QBS_ASSERT(depmod, return);
            moduleParents[depmod].append(module.get());
            moduleChildren[module.get()].append(depmod);
        }
    }

    QList<const ResolvedModule *> rootModules;
    for (const ResolvedModuleConstPtr &module : m_product->modules) {
        if (moduleParents.value(module.get()).isEmpty()) {
            QBS_ASSERT(module, return);
            rootModules.append(module.get());
        }
    }

    EnvProvider envProvider(engine(), m_env);

    Set<QString> seenModuleNames;
    const QList<const ResolvedModule *> &topSortedModules
            = topSortModules(moduleChildren, rootModules, seenModuleNames);
    const QStringList scriptFunctionArgs = m_envType == BuildEnv
            ? ResolvedModule::argumentNamesForSetupBuildEnv()
            : ResolvedModule::argumentNamesForSetupRunEnv();
    for (const ResolvedModule * const module : topSortedModules) {
        const ScriptFunction *setupScript = getScript(module);
        if (setupScript->sourceCode.isEmpty())
            continue;

        RulesEvaluationContext::Scope s(m_evalContext);
        QScriptValue envScriptContext = engine()->newObject();
        envScriptContext.setPrototype(engine()->globalObject());
        setupScriptEngineForProduct(engine(), m_product, module, envScriptContext, nullptr, false);
        static const QString productKey = QStringLiteral("product");
        static const QString projectKey = QStringLiteral("project");
        m_evalContext->scope().setProperty(productKey, envScriptContext.property(productKey));
        m_evalContext->scope().setProperty(projectKey, envScriptContext.property(projectKey));
        if (m_envType == RunEnv) {
            QScriptValue configArray = engine()->newArray(m_runEnvConfig.size());
            for (int i = 0; i < m_runEnvConfig.size(); ++i)
                configArray.setProperty(i, QScriptValue(m_runEnvConfig.at(i)));
            m_evalContext->scope().setProperty(QLatin1String("config"), configArray);
        }
        setupScriptEngineForFile(engine(), setupScript->fileContext, m_evalContext->scope(),
                                 ObserveMode::Disabled);
        QScriptValue fun = engine()->evaluate(setupScript->sourceCode,
                                              setupScript->location.filePath(),
                                              setupScript->location.line());
        QBS_CHECK(fun.isFunction());
        const QScriptValueList svArgs = ScriptEngine::argumentList(scriptFunctionArgs,
                                                                   m_evalContext->scope());
        const QScriptValue res = fun.call(QScriptValue(), svArgs);
        engine()->releaseResourcesOfScriptObjects();
        if (Q_UNLIKELY(engine()->hasErrorOrException(res))) {
            const QString scriptName = QLatin1String(m_envType == BuildEnv ? "setupBuildEnvironment"
                                                                           : "setupRunEnvironment");
            throw ErrorInfo(Tr::tr("Error running %1 script for product '%2': %3")
                            .arg(scriptName, m_product->fullDisplayName(),
                                 engine()->lastErrorString(res)),
                            engine()->lastErrorLocation(res, setupScript->location));
        }
    }

    const QProcessEnvironment &newEnv = envProvider.alteredEnvironment();
    if (m_envType == BuildEnv)
        m_product->buildEnvironment = newEnv;
    else
        m_product->runEnvironment = newEnv;
}

ScriptEngine *EnvironmentScriptRunner::engine() const
{
    return m_evalContext->engine();
}

const ScriptFunction *EnvironmentScriptRunner::getScript(const ResolvedModule *module) const
{
    return (m_envType == BuildEnv
            ? module->setupBuildEnvironmentScript : module->setupRunEnvironmentScript).get();
}

} // namespace Internal
} // namespace qbs