#pragma once
#include "ofMain.h"
#include "ofxOpenCv.h"
#include <opencv2/aruco.hpp>
#include <opencv2/core/core_c.h> // for cvarrToMat (IplImage* -> cv::Mat)

// If your real app already defines these, set the following macros in
// Project Settings → Build Settings → Other C++ Flags (Preprocessor):
//   DW_HAS_USER_PLANE_HALF_SPAN
//   DW_HAS_USER_INJECT_IMPULSE
// and provide:
//   extern float planeHalfSpan;
//   void injectImpulse(const glm::vec2&, float);
class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void keyPressed(int key) override;
    void mousePressed(int x, int y, int button) override;
    void mouseDragged(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseScrolled(int x, int y, float scrollX, float scrollY) override;

    void cycleArucoDevice(int delta);

    // -------- ArUco-tracked paper (4×4 grid) --------
    bool setupArucoTracker(int deviceId = 0, int w = 1280, int h = 720);
    void updateArucoTracker();                 // detect markers, compute H, warp
    void computeGridDarknessFromWarp();        // per-cell average darkness
    void applyPaperGridToFluid();              // darkness → impulses
    void drawArucoDebug(int x=12, int y=12, int w=320);

    bool useArucoTracker = true;

    // Camera
    int  arucoCamW = 1280, arucoCamH = 720;
    int  arucoCamDeviceId = 0;
    ofVideoGrabber      arucoCam;
    ofxCvColorImage     arucoColor;
    ofxCvGrayscaleImage arucoGray;
    std::vector<ofVideoDevice> arucoDevices;

    // ArUco
    cv::Ptr<cv::aruco::Dictionary>         arucoDict;
    cv::Ptr<cv::aruco::DetectorParameters> arucoParams;

    // Expected marker IDs (clockwise TL,TR,BR,BL)
    int markerIdTL = 10, markerIdTR = 11, markerIdBR = 12, markerIdBL = 13;

    // Warp/rectification of the inner square bounded by the markers
    bool      arucoHasH = false;
    cv::Mat   arucoH;                 // homography
    int       arucoWarpW = 480;
    int       arucoWarpH = 480;       // square rectified view of your 4×4 area
    ofPixels  arucoWarpPix;           // 8-bit grayscale, 1 channel

    // Darkness sampling & mapping
    int   arucoGridN = 4;             // 4×4 cells
    std::vector<float> arucoCellDark; // size = 16, 0..1 (white→0, black→1)
    float arucoDarkEps     = 0.03f;   // ignore near-white noise
    float arucoImpulseHz   = 15.0f;   // how often impulses fire
    float arucoMinStrength = 0.0f;    // mapping floor
    float arucoMaxStrength = 0.9f;    // mapping ceil
    float arucoCoverage    = 1.0f;    // fraction of plane covered by 4×4
    float lastArucoImpulseT = 0.0f;
    bool  arucoHadHPrev    = false;
    float arucoLastImpulseLog = 0.0f;
    std::array<int,4> arucoSlotIds = {-1,-1,-1,-1};

    // Map 4×4 cell center to your world XY plane
    glm::vec2 arucoCellCenterWorld(int ci, int cj) const;

#ifndef DW_HAS_USER_PLANE_HALF_SPAN
    float planeHalfSpan = 400.0f; // fallback span (world units)
#endif

#ifndef DW_HAS_USER_INJECT_IMPULSE
    // Fallback ripple demo if you don’t have a sim hooked up yet
    void injectImpulse(const glm::vec2& worldXY, float strength);
    struct Ripple { glm::vec2 p; float t0; float s; };
    std::vector<Ripple> ripples;
    ofMesh planeMesh;
    int    planeRes = 120;
    void   rebuildPlaneMesh();
    void   updatePlaneMesh(float time);
#endif

    ofEasyCam cam;
    bool showDebug = true;
};
