/****************************************************************************
**
** Copyright (C) 2021 Raphaël Cotty <raphael.cotty@gmail.com>
** Contact: http://www.qt.io/licensing
**
** This file is part of the Qt Build Suite.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms and
** conditions see http://www.qt.io/terms-conditions. For further information
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
** In addition, as a special exception, The Qt Company gives you certain additional
** rights.  These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

import qbs
import qbs.Environment
import qbs.File
import qbs.FileInfo
import qbs.ModUtils
import qbs.Probes
import "codesign.js" as CodeSign

CodeSignModule {
    condition: qbs.targetOS.contains("android")
    priority: 1
    enablePackageSigning: true
    property string packageName
    property bool useApksigner: true

    Probes.AndroidSdkProbe {
        id: sdkProbe
        environmentPaths: (sdkDir ? [sdkDir] : []).concat(base)
    }
    property path sdkDir: sdkProbe.path
    property string buildToolsVersion: sdkProbe.buildToolsVersion
    property path buildToolsDir: FileInfo.joinPaths(sdkDir, "build-tools", buildToolsVersion)
    property path apksignerFilePath: FileInfo.joinPaths(buildToolsDir, "apksigner")

    Probes.JdkProbe {
        id: jdk
        environmentPaths: (jdkPath ? [jdkPath] : []).concat(base)
    }
    property string jdkPath: jdk.path
    property string jarsignerFilePath: FileInfo.joinPaths(jdkPath, "bin", jarsignerName)
    property string jarsignerName: "jarsigner"
    property string keytoolFilePath: FileInfo.joinPaths(jdkPath, "bin", keytoolName)
    property string keytoolName: "keytool"

    property string debugKeystorePath: FileInfo.joinPaths(
                                           Environment.getEnv(qbs.hostOS.contains("windows")
                                                              ? "USERPROFILE" : "HOME"),
                                           ".android", "debug.keystore")
    readonly property string debugKeystorePassword: "android"
    readonly property string debugPassword: "android"
    readonly property string debugKeyAlias: "androiddebugkey"

    keystorePath: debugKeystorePath
    keystorePassword: debugKeystorePassword
    keyPassword: debugPassword
    keyAlias: debugKeyAlias

    Rule {
        condition: useApksigner
        inputs: ["android.package_unsigned"]
        Artifact {
            filePath: product.codesign.packageName
            fileTags: "android.package"
        }
        prepare: CodeSign.signApkPackage.apply(this, arguments)
    }

    Rule {
        condition: !useApksigner
        inputs: ["android.package_unsigned"]
        Artifact {
            filePath: product.codesign.packageName
            fileTags: "android.package"
        }
        prepare: CodeSign.signAabPackage.apply(this, arguments)
    }

    validate: {
        // Typically there is a debug keystore in ~/.android/debug.keystore which gets created
        // by the native build tools the first time a build is done. However, we don't want to
        // create it ourselves, because writing to a location outside the qbs build directory is
        // both polluting and has the potential for race conditions. So we'll instruct the user what
        // to do.
        if (keystorePath == debugKeystorePath && !File.exists(debugKeystorePath)) {
            throw ModUtils.ModuleError("Could not find an Android debug keystore at " +
                  codesign.debugKeystorePath + ". " +
                  "If you are developing for Android on this machine for the first time and " +
                  "have never built an application using the native Gradle / Android Studio " +
                  "tooling, this is normal. You must create the debug keystore now using the " +
                  "following command, in order to continue:\n\n" +
                  CodeSign.createDebugKeyStoreCommandString(codesign.keytoolFilePath,
                                                            codesign.debugKeystorePath,
                                                            codesign.debugKeystorePassword,
                                                            codesign.debugPassword,
                                                            codesign.debugKeyAlias) +
                  "\n\n" +
                  "See the following URL for more information: " +
                  "https://developer.android.com/studio/publish/app-signing.html#debug-mode");
        }
    }
}
