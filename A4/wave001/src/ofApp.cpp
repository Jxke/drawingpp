#include "ofApp.h"

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

        ofMesh m; m.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);

        // build like your A/B/C + normal; BUT use **raw points** (no resample),
        // and **normal rotated -90°** so moving left extrudes left.
        for(int i=0; i<(int)poly.size(); ++i){
            int i_m_1 = std::max(0, i-1);
            int i_p_1 = std::min((int)poly.size()-1, i+1);

            ofPoint a = toWorld(poly[i_m_1]);
            ofPoint b = toWorld(poly[i]);
            ofPoint c = toWorld(poly[i_p_1]);

            ofPoint diff = (c - a).getNormalized();
            ofPoint nrm  = diff.getRotated(-90, ofPoint(0,0,1)); // <-- flip side

            // width from recorded **mouse velocity** (px/sec)
            float vel = (i < (int)vels.size()) ? vels[i] : 0.f;
            vel = ofClamp(vel, 0.f, velClamp);
            float halfW = baseW + velGain * vel;

            // height from simple noise along index/time
            float h = ofNoise(float(i)*freq, t);
            float z = (h - 0.5f) * 2.0f * amp;

            ofPoint center(b.x, b.y, z);
            ofPoint vL = center + nrm * halfW;
            ofPoint vR = center - nrm * halfW;

            float crest = ofClamp((z/amp)*0.5f + 0.5f, 0.f, 1.f);
            ofFloatColor col = ofFloatColor(0.16f).getLerped(ofFloatColor(0.92f), crest*0.7f);

            // keep winding consistent: add L then R
            m.addVertex(vL); m.addColor(col);
            m.addVertex(vR); m.addColor(col*ofFloatColor(0.95f));
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
