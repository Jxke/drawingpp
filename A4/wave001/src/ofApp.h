#pragma once
#include "ofMain.h"

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
    std::vector<ofMesh> meshes;

    // params (simple)
    float baseW    = 8.0f;         // base half-width
    float velGain  = 0.025f;       // width += velGain * velocity(px/sec)
    float velClamp = 1200.0f;      // cap velocity used for width

    float amp   = 40.0f;           // height amplitude (Z)
    float freq  = 0.010f;          // noise frequency along samples
    float speed = 0.30f;           // noise time speed

    float t = 0.f;
    bool  paused = false;

    // helpers
    void rebuildMeshes();          // build one triangle strip mesh per stroke
    void updateCamera();           // Z-up turntable
    void resetCamera();
    void drawXYGrid(float step=50.f, int count=20);

    // map screen -> world (XY plane, Z up)
    inline ofPoint toWorld(const ofPoint& p) const {
        return ofPoint(p.x - ofGetWidth()*0.5f, -(p.y - ofGetHeight()*0.5f), 0);
    }
};
