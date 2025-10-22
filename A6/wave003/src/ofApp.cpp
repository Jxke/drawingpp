#include "ofApp.h"
#include <glm/gtc/matrix_transform.hpp> // glm::unProject
#include <glm/gtc/type_ptr.hpp>

//--------------------------------------------------------------
void ofApp::setup(){
    ofSetFrameRate(120);
    ofSetVerticalSync(true);
    ofEnableDepthTest();
    resetCamera();
    waveRes = 2 * planeCount + 1;
    buildWaveMesh();
    setupLighting();
}

//--------------------------------------------------------------
void ofApp::setupLighting(){
    // global ambient so the underside isn’t black
    ofSetGlobalAmbientColor(ofColor(30, 30, 30));

    // Key light (directional)
    keyLight.setDirectional();
    keyLight.setDiffuseColor(ofFloatColor(0.95f, 0.95f, 0.95f));
    keyLight.setSpecularColor(ofFloatColor(0.8f, 0.8f, 0.8f));
    // aim from +X,+Y,+Z toward the origin
    keyLight.setPosition(700, 800, 900);
    keyLight.lookAt(glm::vec3(0,0,0), glm::vec3(0,0,1));

    // Fill (soft, opposite side)
    fillLight.setDirectional();
    fillLight.setDiffuseColor(ofFloatColor(0.35f, 0.35f, 0.40f));
    fillLight.setSpecularColor(ofFloatColor(0.0f, 0.0f, 0.0f));
    fillLight.setPosition(-900, -700, 500);
    fillLight.lookAt(glm::vec3(0,0,0), glm::vec3(0,0,1));

    // Material for the mesh (subtle specular to read surface)
    mat.setShininess(32);
    mat.setDiffuseColor(ofFloatColor(0.25f, 0.25f, 0.25f, 1));
    mat.setSpecularColor(ofFloatColor(0.18f, 0.18f, 0.18f, 1));
    mat.setAmbientColor(ofFloatColor(0.1f, 0.1f, 0.1f, 1));
}

//--------------------------------------------------------------
void ofApp::update(){
    t += paused ? 0.f : ofGetLastFrameTime();
    if(!paused){
        updateWaves(ofGetLastFrameTime());
    }
}
//--------------------------------------------------------------
void ofApp::draw(){
    ofBackground(10);

    updateCamera();
    cam.begin();

    drawWaveMeshLit();

    // axes on top (no Z-fighting)
    drawAxes(400.f);

    const float now = ofGetElapsedTimef();

    bool depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    ofDisableDepthTest();

    ofPushStyle();
    ofSetLineWidth(1.0f);
    
    for (size_t i = 0; i < strokes.size(); ++i) {
        auto &verts = strokes[i].getVertices();
        auto &times = strokeTimes[i];

        // Safety: keep arrays aligned
        if (times.size() != verts.size()) {
            size_t m = std::min(times.size(), verts.size());
            verts.resize(m);
            times.resize(m);
            if (i < strokeVels.size()) strokeVels[i].resize(m);
        }

        // Draw segments with alpha by the YOUNGER vertex (j)
        for (size_t j = 1; j < verts.size(); ++j) {
            float age = now - times[j];                          // vertex j age
            float t   = ofClamp(age / fadeSec, 0.0f, 1.0f);
            float a   = powf(1.0f - t, fadePow);                 // fade curve

            if (a <= 0.0f) continue;

            ofSetColor(255, 255, 255, (int)(a * 255.0f));
            ofDrawLine(verts[j-1], verts[j]);                    // tiny z-lift already in verts
        }

        // Trim fully faded head vertices (keep at least 1 so we don't erase an active stroke head)
        // Remove from the FRONT while next vertex is older than fadeSec
        while (verts.size() > 1 && (now - times[1]) > fadeSec) {
            verts.erase(verts.begin());          // drop oldest
            times.erase(times.begin());
            if (i < strokeVels.size() && !strokeVels[i].empty())
                strokeVels[i].erase(strokeVels[i].begin());
        }
    }

    ofPopStyle();
    if (depthWasEnabled) ofEnableDepthTest();

    cam.end();

    // --- UI text EXACTLY as you had it (bottom-left, brackets string) ---
    ofSetColor(220);
    ofDrawBitmapStringHighlight(
        "[LMB] draw   [RMB drag] orbit   [wheel] zoom   [R] reset view   [C] clear   [space] pause",
        12, ofGetHeight()-18);
}

//--------------------------------------------------------------
void ofApp::drawWaveMeshLit(){
    if(!lightingOn){
        ofSetColor(64);
        waveMesh.draw();
        return;
    }

    // Lit pass
    ofEnableLighting();
    keyLight.enable();
    fillLight.enable();

    mat.begin();
    ofSetColor(255);         // color comes from material; keep this white
    waveMesh.draw();         // solid triangles with normals
    mat.end();

    fillLight.disable();
    keyLight.disable();
    ofDisableLighting();

    // Optional wireframe on top to read curvature
    ofPushStyle();
    ofNoFill();
    ofSetColor(100);
    waveMesh.drawWireframe();
    ofPopStyle();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
    if(key=='c' || key=='C'){
        strokes.clear();
        strokeVels.clear();
        cur = -1;
    } else if(key=='r' || key=='R'){
        resetCamera();
    } else if(key==' '){
        paused = !paused;
    }
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){
    // record for velocity + orbit
    prevMouse.set(x, y);
    prevTime = ofGetElapsedTimef();

    if (button == OF_MOUSE_BUTTON_LEFT){
        bLeftDown = true;

        // start a fresh stroke (no bridging) and aligned metadata
        strokes.emplace_back();
        strokeVels.emplace_back();
        strokeTimes.emplace_back();
        cur = static_cast<int>(strokes.size()) - 1;

        // first vertex on XY plane (z=0) with tiny z-lift for visibility
        glm::vec3 w = screenToXYPlane(x, y);
        strokes[cur].addVertex(glm::vec3(w.x, w.y, lineZLift));
        strokeVels[cur].push_back(0.f);
        strokeTimes[cur].push_back(ofGetElapsedTimef());

        // small initial splash (press usually has near-zero speed)
        injectImpulse(glm::vec2(w.x, w.y), 0.15f);
    }
    else if (button == OF_MOUSE_BUTTON_RIGHT){
        bRightDown = true; // orbit handled in mouseDragged
    }
}void ofApp::mouseDragged(int x, int y, int button){
    const float now = ofGetElapsedTimef();

    if (bLeftDown && button == OF_MOUSE_BUTTON_LEFT && cur >= 0){
        glm::vec3 w = screenToXYPlane(x, y);

        // only append when we've moved far enough (limits verts & impulse spam)
        const auto &verts = strokes[cur].getVertices();
        const float spacing = 4.0f; // keep as-is unless you exposed addPointMinDist
        bool shouldAdd = verts.empty()
                         || glm::distance(verts.back(), glm::vec3(w.x, w.y, lineZLift)) >= spacing;

        if (shouldAdd){
            // screen-space velocity (px/sec) from last event
            float dt = std::max(1e-4f, now - prevTime);
            float v  = ofPoint(x, y).distance(prevMouse) / dt;

            // append vertex + metadata
            strokes[cur].addVertex(glm::vec3(w.x, w.y, lineZLift));
            strokeVels[cur].push_back(v);
            strokeTimes[cur].push_back(now);

            // velocity-scaled splash: slow → gentle, fast → strong
            float vClamped = ofClamp(v, 0.f, velClamp);
            float strength = ofMap(vClamped, 0.f, velClamp, 0.15f, 1.20f, true);
            injectImpulse(glm::vec2(w.x, w.y), strength);
        }
    }
    else if (bRightDown && button == OF_MOUSE_BUTTON_RIGHT){
        // orbit camera (your original pattern)
        ofPoint delta = ofPoint(x, y) - prevMouse;
        const float rotSpeed = 0.25f;           // deg per pixel
        camYawDeg   += delta.x * rotSpeed;
        camPitchDeg -= delta.y * rotSpeed;      // drag up → look down
        camPitchDeg  = ofClamp(camPitchDeg, -89.f, 89.f);
    }

    // update previous input state for next event
    prevMouse.set(x, y);
    prevTime = now;
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){
    if(button == OF_MOUSE_BUTTON_LEFT){
        bLeftDown = false;
        if(cur >= 0 && strokes[cur].size() < 2){
            strokes.pop_back();
            strokeVels.pop_back();
            strokeTimes.pop_back();      // NEW
        }
        cur = -1;
    }
    else if(button == OF_MOUSE_BUTTON_RIGHT){
        bRightDown = false;
    }
}


//--------------------------------------------------------------
void ofApp::mouseScrolled(int x, int y, float scrollX, float scrollY){
    // --- YOUR zoom pattern preserved (distance change on wheel) ---
    float zoomSpeed = 25.0f;
    camDist -= scrollY * zoomSpeed;
    camDist = ofClamp(camDist, 100.f, 5000.f);
}

//--------------------------------------------------------------
void ofApp::buildWaveMesh(){
    // allocate arrays
    H.assign(waveRes * waveRes, 0.f);
    Hprev.assign(waveRes * waveRes, 0.f);
    V.assign(waveRes * waveRes, 0.f);

    waveMesh.clear();
    waveMesh.setMode(OF_PRIMITIVE_TRIANGLES);
    waveMesh.enableIndices();
    waveMesh.enableNormals();

    // vertices on XY plane
    for(int j=0; j<waveRes; ++j){
        float y = ofLerp(-planeHalfSpan, planeHalfSpan, (float)j/(waveRes-1));
        for(int i=0; i<waveRes; ++i){
            float x = ofLerp(-planeHalfSpan, planeHalfSpan, (float)i/(waveRes-1));
            waveMesh.addVertex(glm::vec3(x, y, 0.f));
            waveMesh.addNormal(glm::vec3(0,0,1));
            waveMesh.addColor(ofFloatColor(0.25f, 0.25f, 0.25f, 1.f)); // grey fill
        }
    }
    // indices
    for(int j=0; j<waveRes-1; ++j){
        for(int i=0; i<waveRes-1; ++i){
            int i0 = idx(i,   j);
            int i1 = idx(i+1, j);
            int i2 = idx(i,   j+1);
            int i3 = idx(i+1, j+1);
            waveMesh.addIndex(i0); waveMesh.addIndex(i1); waveMesh.addIndex(i2);
            waveMesh.addIndex(i1); waveMesh.addIndex(i3); waveMesh.addIndex(i2);
        }
    }
}

//--------------------------------------------------------------
void ofApp::updateWaves(float dt){
    if (dt <= 0.f) return;

    // discrete Laplacian on grid (skip borders)
    const float c = waveC;            // stability requires c smallish
    const float damp = waveDamp;

    for(int j=1; j<waveRes-1; ++j){
        for(int i=1; i<waveRes-1; ++i){
            int id = idx(i,j);
            float h  = H[id];
            float n  = H[idx(i, j+1)];
            float s  = H[idx(i, j-1)];
            float e  = H[idx(i+1, j)];
            float w  = H[idx(i-1, j)];
            float lap = (n + s + e + w - 4.f * h);

            // velocity update + damping
            V[id] = (V[id] + c * lap) * damp;
        }
    }
    // integrate height and write back to mesh vertices
    auto &verts = waveMesh.getVertices();
    for(int j=1; j<waveRes-1; ++j){
        for(int i=1; i<waveRes-1; ++i){
            int id = idx(i,j);
            H[id] += V[id];
            verts[id].z = H[id] * waveScale; // displace Z
        }
    }

    // recompute normals (cheap approx)
    auto &norms = waveMesh.getNormals();
    for(int j=1; j<waveRes-1; ++j){
        for(int i=1; i<waveRes-1; ++i){
            int id = idx(i,j);
            glm::vec3 px = waveMesh.getVertex(idx(i+1,j)) - waveMesh.getVertex(idx(i-1,j));
            glm::vec3 py = waveMesh.getVertex(idx(i,j+1)) - waveMesh.getVertex(idx(i,j-1));
            glm::vec3 n = glm::normalize(glm::cross(px, py));
            norms[id] = n;
        }
    }
}

//--------------------------------------------------------------

void ofApp::injectImpulse(const glm::vec2& p, float strength){
    // affect only cells within radius
    int ci, cj;
    if(!worldToIJ(p, ci, cj)) return;

    float r2max = impulseRadius * impulseRadius;
    int rCells = std::max(1, (int)ceil(impulseRadius / (2.f * planeHalfSpan) * (waveRes-1)));

    int i0 = ofClamp(ci - rCells, 1, waveRes-2);
    int i1 = ofClamp(ci + rCells, 1, waveRes-2);
    int j0 = ofClamp(cj - rCells, 1, waveRes-2);
    int j1 = ofClamp(cj + rCells, 1, waveRes-2);

    for(int j=j0; j<=j1; ++j){
        float y = ofLerp(-planeHalfSpan, planeHalfSpan, (float)j/(waveRes-1));
        for(int i=i0; i<=i1; ++i){
            float x = ofLerp(-planeHalfSpan, planeHalfSpan, (float)i/(waveRes-1));
            float dx = x - p.x;
            float dy = y - p.y;
            float r2 = dx*dx + dy*dy;
            if(r2 > r2max) continue;

            float g = expf(-r2 / (2.f * impulseSigma * impulseSigma));  // Gaussian
            int id = idx(i,j);
            // Add to velocity for a cleaner ring wave (push up)
            V[id] += strength * g;
        }
    }
}

//--------------------------------------------------------------
void ofApp::updateCamera(){
    // --- YOUR Z-up turntable camera preserved ---
    float yawRad   = glm::radians(camYawDeg);
    float pitchRad = glm::radians(camPitchDeg);

    float cx = camDist * cosf(pitchRad) * cosf(yawRad);
    float cy = camDist * cosf(pitchRad) * sinf(yawRad);
    float cz = camDist * sinf(pitchRad);

    cam.setPosition(glm::vec3(cx, cy, cz));
    cam.lookAt(glm::vec3(0,0,0), glm::vec3(0,0,1)); // Z-up
    cam.setNearClip(1.0f);
    cam.setFarClip(20000.0f);
}

//--------------------------------------------------------------
void ofApp::resetCamera(){
    camYawDeg   = 45.f;
    camPitchDeg = 35.f;
    camDist     = 900.f;
}

//--------------------------------------------------------------
void ofApp::drawXYGrid(float step, int count){
    // Big grey plane at z=0 (kept as before; can tune color/size)
    ofPushStyle();
    ofSetColor(64); // grey plane
    ofFill();

    float halfSpan = step * count;
    ofDrawPlane(glm::vec3(0,0,0), halfSpan*2.f, halfSpan*2.f); // centered in XY

    // Subtle grid lines
    ofNoFill();
    ofSetLineWidth(1.0f);
    ofSetColor(100);

    ofMesh grid;
    grid.setMode(OF_PRIMITIVE_LINES);

    for(int i=-count; i<=count; ++i){
        float x = i * step;
        // lines parallel to X (vary Y)
        grid.addVertex(glm::vec3(-halfSpan, x, 0));
        grid.addVertex(glm::vec3( halfSpan, x, 0));
        // lines parallel to Y (vary X)
        grid.addVertex(glm::vec3(x, -halfSpan, 0));
        grid.addVertex(glm::vec3(x,  halfSpan, 0));
    }
    grid.draw();

    ofPopStyle();
}

//--------------------------------------------------------------
// NEW: precise screen→XY (z=0) intersection using current camera
glm::vec3 ofApp::screenToXYPlane(int sx, int sy) const {
    const ofRectangle vp = ofGetCurrentViewport();
    const glm::mat4 view = cam.getModelViewMatrix();
    const glm::mat4 proj = cam.getProjectionMatrix(vp);

    const float winX = (float)sx;
    const float winY = vp.getHeight() - (float)sy; // bottom-left origin for unProject
    const glm::vec4 glvp(vp.x, vp.y, vp.width, vp.height);

    glm::vec3 p0 = glm::unProject(glm::vec3(winX, winY, 0.0f), view, proj, glvp);
    glm::vec3 p1 = glm::unProject(glm::vec3(winX, winY, 1.0f), view, proj, glvp);
    glm::vec3 dir = p1 - p0;

    if (fabsf(dir.z) < 1e-7f) return glm::vec3(p0.x, p0.y, 0.0f);

    float t = -p0.z / dir.z;
    glm::vec3 p = p0 + t * dir;
    return glm::vec3(p.x, p.y, 0.0f);
}

//--------------------------------------------------------------
void ofApp::drawAxes(float axisLen){
    bool depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    ofDisableDepthTest();
    ofPushStyle();
    ofSetLineWidth(2.0f);
    ofSetColor(220, 80, 80);  ofDrawLine({0,0,0}, {axisLen,0,0});   // X
    ofSetColor(80, 220,120);  ofDrawLine({0,0,0}, {0,axisLen,0});   // Y
    ofSetColor(80, 140,240);  ofDrawLine({0,0,0}, {0,0,axisLen});   // Z
    ofPopStyle();
    if (depthWasEnabled) ofEnableDepthTest();
}
