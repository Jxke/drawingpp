#pragma once
#include "ofMain.h"

// 21-landmark container compatible with MediaPipe-style indexing.
// Coordinates are in IMAGE PIXELS (not normalized).
struct HandLandmarks {
    bool present = false;
    std::vector<glm::vec2> lm; // size 21 when present
    float handednessScore = 0.f; // if your backend provides it
};

class HandRippleAddon {
public:
    // Public settings
    bool  enabled       = true;   // toggle with 'W' if you like (from ofApp)
    bool  mirrorX       = true;   // selfie-like mirroring
    float scaleThreshold= 0.08f;  // palm width / frame width to "arm" drawing
    int   camW          = 1280;
    int   camH          = 720;
    int   deviceId      = 0;

    // Lifecycle
    void setup(int camWidth=1280, int camHeight=720, int device=0){
        camW = camWidth; camH = camHeight; deviceId = device;
        grabber.setDeviceID(deviceId);
        grabber.setDesiredFrameRate(60);
        grabber.setup(camW, camH);
    }

    // Call every frame; if there is a new frame, run detection shim.
    void update(){
        if(!enabled) return;
        grabber.update();
        if(grabber.isFrameNew()){
            // Fill rightHand/leftHand from your detector here
            HandLandmarks detRight, detLeft;
            runDetector(detRight, detLeft); // <— stubbed below

            rightHand = detRight;
            leftHand  = detLeft;

            if(mirrorX){
                auto mirror = [&](HandLandmarks &h){
                    if(!h.present) return;
                    for(auto &p : h.lm) p.x = float(camW - 1) - p.x;
                };
                mirror(rightHand); mirror(leftHand);
            }
        }
    }

    // Accessors
    const HandLandmarks& getRight() const { return rightHand; }
    const HandLandmarks& getLeft()  const { return leftHand;  }

    // Utilities
    // Palm width proxy for depth: distance between index MCP (5) and pinky MCP (17) normalized by frame width.
    float palmScaleNorm(const HandLandmarks& h) const {
        if(!h.present || h.lm.size() < 18) return 0.f;
        float w = glm::length(h.lm[17] - h.lm[5]);
        return w / float(camW);
    }

    // Index fingertip pixel position (landmark 8). Returns false if not valid.
    bool indexTipPx(const HandLandmarks& h, glm::vec2& outPx) const {
        if(!h.present || h.lm.size() < 9) return false;
        outPx = h.lm[8]; return true;
    }

    // Convert image pixels → your world XY plane coordinates
    // planeHalfSpan: half the plane width (and height) in world units.
    inline glm::vec2 imagePxToWorldXY(const glm::vec2& px, float planeHalfSpan) const {
        float u = px.x / float(camW);
        float v = px.y / float(camH);
        float x = ofLerp(-planeHalfSpan, planeHalfSpan, u);
        float y = ofLerp( planeHalfSpan, -planeHalfSpan, v); // flip Y to world-up
        return {x, y};
    }

    // Optional: draw a tiny debug overlay of the camera + landmarks
    void drawDebug(float x=10, float y=10, float w=320){
        float h = w * (camH/float(camW));
        ofPushStyle();
        ofSetColor(255);
        grabber.draw(x,y,w,h);
        auto drawHand = [&](const HandLandmarks& h, ofColor c){
            if(!h.present || h.lm.size()!=21) return;
            ofSetColor(c);
            float sx = w/float(camW), sy = h/float(camH);
            for(int i=0;i<21;++i){
                ofDrawCircle(x + h.lm[i].x*sx, y + h.lm[i].y*sy, 2.0f);
            }
        };
        drawHand(rightHand, ofColor::limeGreen);
        drawHand(leftHand,  ofColor::orange);
        ofPopStyle();
    }

private:
    ofVideoGrabber grabber;
    HandLandmarks rightHand, leftHand;

    // ============ HAND DETECTOR STUB ============
    // Replace this with your MediaPipe/CoreML integration.
    // Fill detRight/detLeft.lm (size 21) in IMAGE PIXELS and set .present = true as appropriate.
    void runDetector(HandLandmarks& detRight, HandLandmarks& detLeft){
        // ---- MOCK EXAMPLE (disabled) ----
        // Uncomment to sanity-check without a detector:
        /*
        static float a=0; a+=0.04f;
        glm::vec2 center(camW*0.5f, camH*0.5f);
        glm::vec2 tip   = center + glm::vec2(cos(a), sin(a))* (camH*0.25f);
        detRight.present = true;
        detRight.handednessScore = 0.9f;
        detRight.lm.assign(21, center);
        detRight.lm[8]  = tip;                  // index tip
        detRight.lm[5]  = center + glm::vec2(-60,0); // index MCP
        detRight.lm[17] = center + glm::vec2(60,0);  // pinky MCP
        */
    }
};
