#pragma once
// What the board / schematic readers produce and EdaViewer draws. No Windows headers: the readers also build
// on Linux (see linux/).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Explorer2 {

enum class EdaDocKind : int {
    None = 0,
    KiCadPcb = 1,
    KiCadSch = 2,
    EagleBrd = 3,
    EagleSch = 4,
    GerberLayer = 5,
    ExcellonDrill = 6,
    AutoCadDxf = 7
};

enum class EdaLayerId : uint8_t {
    TopCopper = 0,
    BottomCopper = 1,
    InnerCopper = 2,
    TopSilk = 3,
    BottomSilk = 4,
    EdgeCuts = 5,
    PadsThrough = 6,
    Drills = 7,
    SchWire = 8,
    SchSymbol = 9,
    SchLabel = 10
};

enum class EdaPartCategory : uint8_t {
    GenericSMD = 0,
    IC_Package = 1,
    ResistorSMD = 2,
    CapacitorSMD = 3,
    ElectrolyticCap = 4,
    Connector = 5,
    TransistorSOT = 6,
    DiodeLED = 7,
    Inductor = 8,
    Crystal = 9,
    MountingHole = 10
};

struct EdaTraceSeg {
    float x1 = 0.0f, y1 = 0.0f;
    float x2 = 0.0f, y2 = 0.0f;
    float width = 0.25f;
    EdaLayerId layer = EdaLayerId::TopCopper;
    int netId = 0;
};

struct EdaCirclePrim {
    float cx = 0.0f, cy = 0.0f;
    float radius = 1.0f;
    float width = 0.2f;
    bool filled = false;
    EdaLayerId layer = EdaLayerId::TopSilk;
};

struct EdaPadPrim {
    float x = 0.0f, y = 0.0f;
    float w = 1.2f, h = 1.2f;
    float rotDeg = 0.0f;
    float drill = 0.0f;
    uint8_t shape = 0; // 0=circle, 1=rect, 2=roundrect, 3=oval
    EdaLayerId layer = EdaLayerId::TopCopper;
    std::wstring pinName;
    std::wstring netName;
};

struct EdaViaPrim {
    float x = 0.0f, y = 0.0f;
    float diameter = 0.8f;
    float drill = 0.4f;
    int netId = 0;
};

struct EdaTextLabel {
    float x = 0.0f, y = 0.0f;
    float size = 1.2f;
    EdaLayerId layer = EdaLayerId::TopSilk;
    std::wstring text;
};

struct EdaComponent3D {
    std::wstring refDes;
    std::wstring value;
    std::wstring footprint;
    float x = 0.0f, y = 0.0f;
    float rotDeg = 0.0f;
    float widthMm = 3.0f;
    float lengthMm = 2.0f;
    float heightMm = 1.2f;
    bool bottomSide = false;
    EdaPartCategory category = EdaPartCategory::GenericSMD;
    int pinCount = 2;
};

struct ParsedEdaBoard {
    std::wstring filePath;
    std::wstring formatTitle;
    EdaDocKind kind = EdaDocKind::None;
    bool valid = false;

    std::vector<EdaTraceSeg> traces;
    std::vector<EdaCirclePrim> circles;
    std::vector<EdaPadPrim> pads;
    std::vector<EdaViaPrim> vias;
    std::vector<EdaTextLabel> labels;
    std::vector<EdaComponent3D> components;

    float minX = -25.0f, minY = -20.0f;
    float maxX = 25.0f,  maxY = 20.0f;
    size_t netCount = 0;
};

} // namespace Explorer2
