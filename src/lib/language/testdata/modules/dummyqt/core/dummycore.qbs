import qbs 1.0

Module {
    id: qtcore
    property int versionMajor: 5
    property int versionMinor: 0
    property int versionPatch: 0
    property string version: versionMajor.toString() + "." + versionMinor.toString() + "." + versionPatch.toString()
    property string coreProperty: "coreProperty"
    property string coreVersion: qtcore.version
}
