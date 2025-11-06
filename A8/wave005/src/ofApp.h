#pragma once
#include "ofMain.h"
#include <array>

class ofApp : public ofBaseApp{
public:
    void setup() override;
    void update() override;
    void draw() override;

    void keyPressed(int key) override;
    void mousePressed(int x, int y, int button) override;
    void mouseDragged(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseScrolled(int x, int y, float scrollX, float scrollY) override;

    // multiple strokes so new clicks don't bridge
    std::vector<ofPolyline> strokes;
    std::vector<std::vector<float>> strokeVels;   // px/sec per vertex (matches strokes[idx])
    int cur = -1;                                  // current stroke index

    // for velocity calc
    ofPoint prevMouse;
    float   prevTime = 0.f;
    bool    bLeftDown = false;
    bool    bRightDown = false;

    // Z-up “turntable” camera (Blender-ish)
    ofCamera cam;
    float camYawDeg   = 45.f;
    float camPitchDeg = 35.f;      // above the ground, looking down
    float camDist     = 900.f;

    // one mesh per stroke so strips never connect
//    std::vector<ofMesh> meshes;

    // params
    float baseW    = 8.0f;         // base half-width
    float velGain  = 0.025f;       // width += velGain * velocity(px/sec)
    float velClamp = 1200.0f;      // cap velocity used for width

    float amp   = 40.0f;           // height amplitude (Z)
    float freq  = 0.010f;          // noise frequency along samples
    float speed = 0.30f;           // noise time speed

    float t = 0.f;
    bool  paused = false;
    
    // Lighting
    ofLight keyLight;
    ofLight fillLight;
    ofMaterial mat;
    bool lightingOn = true;

    // helpers
    void updateCamera();           // Z-up turntable
    void resetCamera();
    void drawXYGrid(float step=50.f, int count=20);
    void drawWaveMeshLit();
    void setupLighting();  // call once in setup()

    // map screen -> world (XY plane, Z up) [kept for compatibility; not used for drawing]
    inline ofPoint toWorld(const ofPoint& p) const {
        return ofPoint(p.x - ofGetWidth()*0.5f, -(p.y - ofGetHeight()*0.5f), 0);
    }
    
    // ----- Wave surface (height-field) -----
    int    waveRes = 161;          // grid resolution per side (odd number: 2*count+1 is neat)
    float  planeStep = 50.f;       // matches your drawXYGrid step
    int    planeCount = 40;        // matches your drawXYGrid count
    float  planeHalfSpan = planeStep * planeCount;  // derived

    ofMesh waveMesh;               // the grey plane as a displaced mesh (triangles)
    std::vector<float> H;          // height (current)
    std::vector<float> Hprev;      // height (previous), for wave equation
    std::vector<float> V;          // velocity (optional; used here to keep it stable)

    // Wave sim params (tweak to taste)
    float  waveC      = 0.22f;     // wave speed factor (stable in [0..~0.5] for this scheme)
    float  waveDamp   = 0.996f;    // per-step damping (closer to 1 = longer ringing)
    float  waveScale  = 10.0f;      // scales Z displacement when rendering
    float  impulseRadius = 120.f;  // world-units radius affected per splash
    float  impulseSigma  = 30.f;   // Gaussian sigma (≈ radius/2)

    // --- methods ---
    void   buildWaveMesh();                    // create the XY grid mesh + arrays
    void   updateWaves(float dt);              // advance sim
    void   injectImpulse(const glm::vec2& p, float strength);  // add splash at XY position

    // utility: world XY → grid ij
    inline bool worldToIJ(const glm::vec2& p, int& i, int& j) const {
        // map [-halfSpan, halfSpan] to [0 .. waveRes-1]
        float u = (p.x + planeHalfSpan) / (2.f * planeHalfSpan); // 0..1
        float v = (p.y + planeHalfSpan) / (2.f * planeHalfSpan); // 0..1
        i = int(round(u * (waveRes - 1)));
        j = int(round(v * (waveRes - 1)));
        return (i >= 0 && i < waveRes && j >= 0 && j < waveRes);
    }
    inline int idx(int i, int j) const { return j * waveRes + i; }


private:
    // precise screen→XY(z=0) (fixes inversion)
    glm::vec3 screenToXYPlane(int sx, int sy) const;  // (already added earlier—keep it)

    // NEW: axes helper (draws without depth test to avoid Z-fighting)
    void drawAxes(float axisLen = 400.f);

    // Camera + ArUco helpers
    void setupCamera();
    void startCamera(int deviceIndex);
    void cycleCamera();
    void processCameraFrame();
    void applyPatternToFluid(const std::array<float,16>& strengths, float dt);
    void drawCameraPreview(float margin = 14.f, float targetWidth = 240.f);

    std::vector<ofMesh> meshes;

    // NEW: per-vertex timestamps (seconds) aligned with strokes[i].getVertices()
    std::vector<std::vector<float>> strokeTimes;

    // fade controls
    float fadeSec = 2.0f;        // older segments vanish after this many seconds
    float fadePow = 1.1f;        // 1.0 = linear, >1 = ease-out steeper

    // keep line lift to avoid Z-fighting with plane
    float lineZLift = 0.5f;

    // ---- Camera + ArUco pattern detection ----
    ofVideoGrabber videoGrabber;
    std::vector<ofVideoDevice> videoDevices;
    int currentVideoDevice = -1;

    bool patternVisible = false;
    std::array<float,16> patternStrengths{};
    float gridWorldHalfSize = 0.f;
    float patternImpulseGain = 20.0f;
    float patternIntensityThreshold = 0.7f;
    float lastPatternImpulseLogTime = 0.f;
    float patternImpulseLogCooldown = 0.5f;

    ofTexture cameraTexture;
    bool cameraFrameReady = false;
    bool previewPatternVisible = false;
    std::vector<std::array<glm::vec2,4>> previewMarkerPolys;
    std::array<glm::vec2,4> previewInnerQuad{};
    int lastMarkerCountLogged = -1;
};
