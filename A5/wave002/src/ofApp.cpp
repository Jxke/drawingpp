#include "ofApp.h"

static float fbm(float x, float y){
    float v=0.f, a=0.5f, f=1.f;
    for(int o=0;o<3;++o){ v += a*ofSignedNoise(x*f, y*f); a*=0.5f; f*=2.f; }
    return v; // [-1,1]
}

void ofApp::setup(){
    ofSetVerticalSync(true);
    ofEnableDepthTest();
    ofBackground(10);

    cam.setNearClip(0.1f);
    cam.setFarClip(5000.f);
    resetCamera();
}

void ofApp::resetCamera(){
    camYawDeg   = 45.f;
    camPitchDeg = 35.f;
    camDist     = 900.f;
    updateCamera();
}

void ofApp::updateCamera(){
    float yaw   = ofDegToRad(camYawDeg);
    float pitch = ofDegToRad(ofClamp(camPitchDeg, 5.f, 85.f));
    float cp = cosf(pitch), sp = sinf(pitch);
    float cy = cosf(yaw),   sy = sinf(yaw);

    ofVec3f pos(camDist * cp * cy,
                camDist * cp * sy,
                camDist * sp);     // Z up
    cam.setPosition(pos);
    cam.lookAt({0,0,0}, {0,0,1});
}

void ofApp::update(){
    if(!paused) t += ofGetLastFrameTime() * speed;
    rebuildMeshes();
    updateCamera();
}

void ofApp::rebuildMeshes(){
    meshes.clear();
    meshes.reserve(strokes.size());

    for(size_t s=0; s<strokes.size(); ++s){
        const auto& poly = strokes[s];
        const auto& vels = strokeVels[s];
        if(poly.size() < 2) { meshes.emplace_back(); continue; }

        ofMesh m;
        m.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);

        // how detailed the “foam sheet” is across the stroke
        const int COLS = 24;               // 16–32 is plenty
        const float jitter = 6.0f;         // lateral wobble (pixels)

        // we build a grid along the stroke (rows=i) and across its width (cols)
        for(int i=0; i<(int)poly.size()-1; ++i){
            // row i (prev/cur/next) — same way you already compute A/B/C & normal
            int i_m_1 = std::max(0, i-1);
            int i_p_1 = std::min((int)poly.size()-1, i+1);

            ofPoint a0 = toWorld(poly[i_m_1]);
            ofPoint b0 = toWorld(poly[i]);
            ofPoint c0 = toWorld(poly[i_p_1]);
            ofPoint nrm0 = (c0 - a0).getNormalized().getRotated(-90, {0,0,1});

            // row i+1
            int j_m_1 = std::max(0, i);
            int j_p_1 = std::min((int)poly.size()-1, i+2);
            ofPoint a1 = toWorld(poly[j_m_1]);
            ofPoint b1 = toWorld(poly[i+1]);
            ofPoint c1 = toWorld(poly[j_p_1]);
            ofPoint nrm1 = (c1 - a1).getNormalized().getRotated(-90, {0,0,1});

            // width from your recorded velocity (keeps your “thicker when faster” look)
            auto velAt = [&](int idx){
                float v = (idx < (int)vels.size()) ? vels[idx] : 0.f;
                return ofClamp(v, 0.f, velClamp);
            };
            float halfW0 = baseW + velGain * velAt(i);
            float halfW1 = baseW + velGain * velAt(i+1);

            // one triangle strip connecting row i and row i+1
            for(int c=0; c<COLS; ++c){
                float u = (COLS==1)?0.0f : (float)c/(COLS-1);   // [0..1]
                float s0 = ofLerp(-1.f, 1.f, u);               // span across width

                // fbm keys (use your freq + time ‘t’ for coherence)
                float k0 = float(i)   * freq;
                float k1 = float(i+1) * freq;
                float ku = u*2.0f;

                // lateral wobble + height are both noise-driven
                float n0 = fbm(k0, t + ku);
                float n1 = fbm(k1, t + ku);

                float z0 = amp * n0;
                float z1 = amp * n1;

                ofPoint p0 = { b0.x, b0.y, z0 };
                ofPoint p1 = { b1.x, b1.y, z1 };

                // base span across the normal, plus jitter from noise
                p0 += nrm0 * (s0 * halfW0 + jitter * n0);
                p1 += nrm1 * (s0 * halfW1 + jitter * n1);

                // foam-ish shading by crest height (same idea you had)
                auto crestCol = [&](float z){
                    float crest = ofClamp((z/amp)*0.5f + 0.5f, 0.f, 1.f);
                    return ofFloatColor(0.16f).getLerped(ofFloatColor(0.92f), crest*0.7f);
                };

                m.addVertex(p0); m.addColor(crestCol(z0));
                m.addVertex(p1); m.addColor(crestCol(z1)*ofFloatColor(0.95f));
            }

            // add a degenerate pair to separate strips (avoids stitching across rows)
            if(i < (int)poly.size()-2){
                m.addVertex(m.getVertices().back());
                m.addColor (m.getColors().back());
                // and start next strip with its first pair on the next loop
            }
        }

        meshes.push_back(std::move(m));
    }
}

void ofApp::drawXYGrid(float step, int count){
    ofPushStyle();
    // axes (X red, Y green, Z blue)
    ofSetLineWidth(2.f);
    ofSetColor(220,80,80);   ofDrawLine(-step*count,0,0,  step*count,0,0); // X
    ofSetColor(80,220,80);   ofDrawLine(0,-step*count,0,  0,step*count,0); // Y
    ofSetColor(120,120,240); ofDrawLine(0,0,-step*count,  0,0,step*count); // Z

    // XY ground
    ofSetColor(255,28);
    ofSetLineWidth(1.f);
    float half = step*count;
    for(int i=-count;i<=count;++i){
        float x=i*step; ofDrawLine(x,-half,0, x,half,0);
        float y=i*step; ofDrawLine(-half,y,0, half,y,0);
    }
    ofPopStyle();
}

void ofApp::draw(){
    ofEnableDepthTest();
    cam.begin();

    drawXYGrid(50.f, 20);
    for(auto& m : meshes) m.draw();

    cam.end();

    ofDisableDepthTest();
    ofSetColor(230,120);
    for(auto& pl : strokes) pl.draw();

    ofDrawBitmapStringHighlight(
        "[LMB] draw   [RMB drag] orbit   [wheel] zoom   [R] reset view   [C] clear   [space] pause",
        12, ofGetHeight()-18);
}

void ofApp::keyPressed(int key){
    if(key=='c' || key=='C'){ strokes.clear(); strokeVels.clear(); meshes.clear(); cur=-1; }
    if(key==' ') paused = !paused;
    if(key=='r' || key=='R') resetCamera();
}

void ofApp::mousePressed(int x, int y, int button){
    prevMouse.set(x,y);
    prevTime = ofGetElapsedTimef();

    if(button == OF_MOUSE_BUTTON_LEFT){
        // start a NEW stroke (no bridging)
        strokes.push_back(ofPolyline());
        strokeVels.push_back(std::vector<float>());
        cur = (int)strokes.size()-1;

        strokes[cur].addVertex(x,y);
        strokeVels[cur].push_back(0.f);   // first point: 0 velocity
        bLeftDown = true;
    }else if(button == OF_MOUSE_BUTTON_RIGHT){
        bRightDown = true;
    }
}

void ofApp::mouseDragged(int x, int y, int button){
    if(button == OF_MOUSE_BUTTON_LEFT && bLeftDown && cur >= 0){
        float now = ofGetElapsedTimef();
        float dt  = std::max(1e-5f, now - prevTime);
        ofPoint curP(x,y);
        float v   = (curP - prevMouse).length() / dt;  // px/sec

        strokes[cur].addVertex(x,y);
        strokeVels[cur].push_back(v);

        prevMouse = curP;
        prevTime  = now;
    }else if(button == OF_MOUSE_BUTTON_RIGHT && bRightDown){
        ofPoint d = ofPoint(x,y) - prevMouse;
        float sens = 0.2f;
        camYawDeg   -= d.x * sens;        // right drag -> orbit right
        camPitchDeg += d.y * sens;        // up drag   -> look more from above
        camPitchDeg  = ofClamp(camPitchDeg, 5.f, 85.f);
        prevMouse.set(x,y);
    }
}

void ofApp::mouseReleased(int x, int y, int button){
    if(button == OF_MOUSE_BUTTON_LEFT)  bLeftDown  = false;
    if(button == OF_MOUSE_BUTTON_RIGHT) bRightDown = false;
}

void ofApp::mouseScrolled(int, int, float, float scrollY){
    camDist = ofClamp(camDist - scrollY*30.f, 150.f, 5000.f);
}
