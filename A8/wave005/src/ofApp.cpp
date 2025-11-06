#include "ofApp.h"
#include <glm/gtc/matrix_transform.hpp> // glm::unProject
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <opencv2/imgproc.hpp>

//--------------------------------------------------------------
void ofApp::setup(){
    ofSetFrameRate(120);
    ofSetVerticalSync(true);
    ofEnableDepthTest();
    resetCamera();
    waveRes = 2 * planeCount + 1;
    buildWaveMesh();
    setupLighting();

    patternStrengths.fill(0.f);
    gridWorldHalfSize = planeHalfSpan * 0.95f;
    setupCamera();
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
    if(videoGrabber.isInitialized()){
        videoGrabber.update();
        processCameraFrame();
    }

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

    drawCameraPreview();

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
    } else if(key=='v' || key=='V'){
        cycleCamera();
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
void ofApp::setupCamera(){
    videoDevices = videoGrabber.listDevices();
    if(videoDevices.empty()){
        ofLogWarning("Camera") << "No video devices found.";
        return;
    }

    int firstAvailable = 0;
    for(size_t i = 0; i < videoDevices.size(); ++i){
        if(videoDevices[i].bAvailable){
            firstAvailable = static_cast<int>(i);
            break;
        }
    }

    startCamera(firstAvailable);
}

//--------------------------------------------------------------
void ofApp::startCamera(int deviceIndex){
    if(deviceIndex < 0 || deviceIndex >= static_cast<int>(videoDevices.size())){
        ofLogWarning("Camera") << "Requested device index " << deviceIndex << " is out of range.";
        return;
    }

    if(videoGrabber.isInitialized()){
        videoGrabber.close();
    }

    const auto &device = videoDevices[deviceIndex];
    videoGrabber.setDeviceID(device.id);
    videoGrabber.setUseTexture(false);
    videoGrabber.setDesiredFrameRate(30);

    if(videoGrabber.setup(1280, 720)){
        currentVideoDevice = deviceIndex;
        patternVisible = false;
        previewPatternVisible = false;
        previewMarkerPolys.clear();
        cameraFrameReady = false;
        lastMarkerCountLogged = -1;
        ofLogNotice("Camera") << "Streaming from device " << device.id << " (" << device.deviceName << ")";
    } else {
        ofLogWarning("Camera") << "Failed to start device " << device.id << " (" << device.deviceName << ")";
    }
}

//--------------------------------------------------------------
void ofApp::cycleCamera(){
    if(videoDevices.empty()){
        ofLogWarning("Camera") << "No cameras available to cycle.";
        return;
    }

    int nextIndex = currentVideoDevice < 0
                    ? 0
                    : (currentVideoDevice + 1) % static_cast<int>(videoDevices.size());

    ofLogNotice("Camera") << "Cycling webcam (V key) to index " << nextIndex;
    startCamera(nextIndex);
}

//--------------------------------------------------------------
void ofApp::processCameraFrame(){
    if(!videoGrabber.isInitialized() || !videoGrabber.isFrameNew()){
        return;
    }

    ofPixels &pixels = videoGrabber.getPixels();
    if(!pixels.isAllocated()){
        return;
    }

    if(!cameraTexture.isAllocated()
       || cameraTexture.getWidth() != pixels.getWidth()
       || cameraTexture.getHeight() != pixels.getHeight()){
        cameraTexture.allocate(pixels.getWidth(), pixels.getHeight(), GL_RGB);
    }
    cameraTexture.loadData(pixels);
    cameraFrameReady = true;

    cv::Mat frameRgb(pixels.getHeight(), pixels.getWidth(), CV_8UC3, pixels.getData());
    cv::Mat frameGray;
    cv::cvtColor(frameRgb, frameGray, cv::COLOR_RGB2GRAY);

    cv::Mat blurred;
    cv::GaussianBlur(frameGray, blurred, cv::Size(5,5), 0);

    cv::Mat binary;
    cv::adaptiveThreshold(blurred, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY_INV, 31, 7);
    cv::Mat morphKernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3,3));
    cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, morphKernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    struct DetectedMarker{
        int id = -1;
        std::array<cv::Point2f,4> corners{};
        int rotation = 0; // clockwise rotations applied to align to canonical pattern
        double area = 0.0;
    };

    static const std::array<std::pair<int, std::array<int,16>>,4> markerPatterns = {{
        {10, {1,1,1,1, 1,0,0,1, 1,0,0,1, 0,0,0,1}},
        {11, {0,0,0,1, 0,0,0,1, 1,0,1,0, 0,1,1,1}},
        {12, {0,0,0,0, 1,1,1,0, 1,0,1,1, 0,1,1,1}},
        {13, {0,0,1,0, 1,0,1,0, 0,0,0,0, 1,1,1,1}}
    }};

    static const std::unordered_map<int,int> slotForId = {
        {10, 0}, // TL
        {11, 1}, // TR
        {12, 2}, // BR
        {13, 3}  // BL
    };

    auto reorderCorners = [](const std::vector<cv::Point>& poly){
        std::array<cv::Point2f,4> ordered{};
        std::array<cv::Point2f,4> pts{};
        for(size_t i = 0; i < 4; ++i){
            pts[i] = cv::Point2f(static_cast<float>(poly[i].x), static_cast<float>(poly[i].y));
        }

        float minSum = std::numeric_limits<float>::max();
        float maxSum = std::numeric_limits<float>::lowest();
        float minDiff = std::numeric_limits<float>::max();
        float maxDiff = std::numeric_limits<float>::lowest();

        for(const auto &pt : pts){
            float sum = pt.x + pt.y;
            float diff = pt.x - pt.y;
            if(sum < minSum){ minSum = sum; ordered[0] = pt; } // top-left
            if(sum > maxSum){ maxSum = sum; ordered[2] = pt; } // bottom-right
            if(diff < minDiff){ minDiff = diff; ordered[1] = pt; } // top-right
            if(diff > maxDiff){ maxDiff = diff; ordered[3] = pt; } // bottom-left
        }
        return ordered;
    };

    auto rotateBitsCW = [](const std::array<int,16>& bits){
        std::array<int,16> rotated{};
        for(int r = 0; r < 4; ++r){
            for(int c = 0; c < 4; ++c){
                rotated[c * 4 + (3 - r)] = bits[r * 4 + c];
            }
        }
        return rotated;
    };

    auto hammingDistance = [](const std::array<int,16>& a, const std::array<int,16>& b){
        int d = 0;
        for(size_t i = 0; i < a.size(); ++i){
            if(a[i] != b[i]) ++d;
        }
        return d;
    };

    std::unordered_map<int, DetectedMarker> markers;
    previewMarkerPolys.clear();
    previewPatternVisible = false;

    for(const auto &contour : contours){
        double perimeter = cv::arcLength(contour, true);
        if(perimeter <= 0.0) continue;

        std::vector<cv::Point> approx;
        cv::approxPolyDP(contour, approx, 0.02 * perimeter, true);
        if(approx.size() != 4 || !cv::isContourConvex(approx)) continue;

        double area = std::fabs(cv::contourArea(approx));
        if(area < 500.0) continue;

        cv::Rect bounds = cv::boundingRect(approx);
        if(bounds.width <= 0 || bounds.height <= 0) continue;
        float ratio = static_cast<float>(bounds.width) / static_cast<float>(bounds.height);
        if(ratio < 0.6f || ratio > 1.4f) continue;

        std::array<cv::Point2f,4> ordered = reorderCorners(approx);

        const int markerSize = 120;
        cv::Point2f dstPts[4] = {
            {0.f, 0.f},
            {static_cast<float>(markerSize), 0.f},
            {static_cast<float>(markerSize), static_cast<float>(markerSize)},
            {0.f, static_cast<float>(markerSize)}
        };

        cv::Mat warp = cv::getPerspectiveTransform(ordered.data(), dstPts);
        cv::Mat markerWarp;
        cv::warpPerspective(frameGray, markerWarp, warp, cv::Size(markerSize, markerSize));
        cv::threshold(markerWarp, markerWarp, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

        const int cells = 4;
        const int borderBits = 1;
        float cellSize = static_cast<float>(markerSize) / static_cast<float>(cells + borderBits * 2);
        std::array<int,16> sampled{};
        bool samplingOk = true;
        for(int r = 0; r < cells && samplingOk; ++r){
            for(int c = 0; c < cells; ++c){
                float sampleX = (c + borderBits + 0.5f) * cellSize;
                float sampleY = (r + borderBits + 0.5f) * cellSize;
                int ix = std::clamp(static_cast<int>(std::round(sampleX)), 0, markerSize - 1);
                int iy = std::clamp(static_cast<int>(std::round(sampleY)), 0, markerSize - 1);
                if(ix < 0 || ix >= markerSize || iy < 0 || iy >= markerSize){
                    samplingOk = false;
                    break;
                }
                sampled[r * cells + c] = markerWarp.at<unsigned char>(iy, ix) < 128 ? 1 : 0;
            }
        }
        if(!samplingOk) continue;

        int matchedId = -1;
        int matchedRot = 0;
        int matchedDist = std::numeric_limits<int>::max();

        const int maxHamming = 4;
        for(const auto &pattern : markerPatterns){
            std::array<int,16> rotated = sampled;
            for(int rot = 0; rot < 4; ++rot){
                int dist = hammingDistance(rotated, pattern.second);
                if(dist < matchedDist){
                    matchedDist = dist;
                    matchedId = pattern.first;
                    matchedRot = rot;
                }
                rotated = rotateBitsCW(rotated);
            }
        }
        if(matchedId == -1 || matchedDist > maxHamming) continue;

        auto existing = markers.find(matchedId);
        if(existing == markers.end() || area > existing->second.area){
            DetectedMarker detection;
            detection.id = matchedId;
            detection.corners = ordered;
            detection.rotation = matchedRot;
            detection.area = area;
            markers[matchedId] = detection;
        }
    }

    for(const auto &entry : markers){
        std::array<glm::vec2,4> poly;
        for(int i = 0; i < 4; ++i){
            poly[i] = glm::vec2(entry.second.corners[i].x, entry.second.corners[i].y);
        }
        previewMarkerPolys.push_back(poly);
    }

    int markerCount = static_cast<int>(markers.size());
    if(markerCount != lastMarkerCountLogged){
        ofLogNotice("Aruco") << "Markers recognized: " << markerCount;
        lastMarkerCountLogged = markerCount;
    }

    std::array<cv::Point2f,4> innerCorners{};
    std::array<bool,4> haveInner{};
    for(const auto &entry : slotForId){
        auto it = markers.find(entry.first);
        if(it == markers.end()) continue;
        const auto &det = it->second;
        int slot = entry.second;
        glm::vec2 markerCenter(0.f);
        for(int i = 0; i < 4; ++i){
            markerCenter += glm::vec2(det.corners[i].x, det.corners[i].y);
        }
        markerCenter *= 0.25f;

        int closestIdx = 0;
        float bestDist = std::numeric_limits<float>::max();
        for(int i = 0; i < 4; ++i){
            float dx = det.corners[i].x - markerCenter.x;
            float dy = det.corners[i].y - markerCenter.y;
            float dist2 = dx*dx + dy*dy;
            if(dist2 < bestDist){
                bestDist = dist2;
                closestIdx = i;
            }
        }
        innerCorners[slot] = det.corners[closestIdx];
        haveInner[slot] = true;
    }

    int knownCorners = std::count(haveInner.begin(), haveInner.end(), true);
    bool foundPattern = (knownCorners >= 3);
    if(foundPattern){
        auto fillMissing = [&](int missing, int a, int b, int c){
            if(!haveInner[missing] && haveInner[a] && haveInner[b] && haveInner[c]){
                innerCorners[missing] = innerCorners[a] + innerCorners[c] - innerCorners[b];
                haveInner[missing] = true;
            }
        };
        fillMissing(0, 1, 2, 3); // TL
        fillMissing(1, 0, 3, 2); // TR
        fillMissing(2, 1, 0, 3); // BR
        fillMissing(3, 2, 1, 0); // BL
        foundPattern = std::all_of(haveInner.begin(), haveInner.end(), [](bool b){ return b; });
    }

    if(foundPattern){
        if(!patternVisible){
            ofLogNotice("Aruco") << "Pattern detected (>=3 markers).";
        }
        patternVisible = true;

        const int warpSize = 320;
        cv::Point2f dstPts[4] = {
            {0.f, 0.f},
            {static_cast<float>(warpSize), 0.f},
            {static_cast<float>(warpSize), static_cast<float>(warpSize)},
            {0.f, static_cast<float>(warpSize)}
        };
        cv::Mat gridWarp = cv::getPerspectiveTransform(innerCorners.data(), dstPts);

        cv::Mat warpedGrid;
        cv::warpPerspective(frameGray, warpedGrid, gridWarp, cv::Size(warpSize, warpSize));
        cv::GaussianBlur(warpedGrid, warpedGrid, cv::Size(5,5), 0);

        std::array<float,16> strengths{};
        int cellSize = warpSize / 4;
        for(int r = 0; r < 4; ++r){
            for(int c = 0; c < 4; ++c){
                cv::Rect roi(c * cellSize, r * cellSize, cellSize, cellSize);
                cv::Scalar meanVal = cv::mean(warpedGrid(roi));
                float darkness = ofClamp(1.f - static_cast<float>(meanVal[0]) / 255.f, 0.f, 1.f);
                strengths[r * 4 + c] = darkness;
            }
        }

        patternStrengths = strengths;
        applyPatternToFluid(strengths, ofGetLastFrameTime());
        for(int i = 0; i < 4; ++i){
            previewInnerQuad[i] = glm::vec2(innerCorners[i].x, innerCorners[i].y);
        }
        previewPatternVisible = true;
    } else {
        if(patternVisible){
            ofLogNotice("Aruco") << "Pattern lost.";
        }
        patternVisible = false;
        patternStrengths.fill(0.f);
        previewPatternVisible = false;
        for(auto &pt : previewInnerQuad){
            pt = glm::vec2(0.f);
        }
    }
}

//--------------------------------------------------------------
void ofApp::drawCameraPreview(float margin, float targetWidth){
    if(!videoGrabber.isInitialized() || !cameraFrameReady || !cameraTexture.isAllocated()){
        return;
    }

    float srcW = static_cast<float>(cameraTexture.getWidth());
    float srcH = static_cast<float>(cameraTexture.getHeight());
    if(srcW <= 0.f || srcH <= 0.f){
        return;
    }

    float previewW = targetWidth;
    float previewH = previewW * (srcH / srcW);

    bool depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    if(depthEnabled) ofDisableDepthTest();

    ofPushStyle();
    ofSetColor(255);
    cameraTexture.draw(margin, margin, previewW, previewH);

    ofNoFill();
    ofSetLineWidth(2.0f);
    ofSetColor(previewPatternVisible ? ofColor(80,220,120) : ofColor(220,80,80));
    ofDrawRectangle(margin, margin, previewW, previewH);

    float scaleX = previewW / srcW;
    float scaleY = previewH / srcH;

    ofSetLineWidth(1.5f);
    ofSetColor(255, 220, 80);
    for(const auto &poly : previewMarkerPolys){
        ofBeginShape();
        for(const auto &pt : poly){
            ofVertex(margin + pt.x * scaleX, margin + pt.y * scaleY);
        }
        ofEndShape(true);
    }

    if(previewPatternVisible){
        ofSetColor(80, 160, 255);
        ofBeginShape();
        for(const auto &pt : previewInnerQuad){
            ofVertex(margin + pt.x * scaleX, margin + pt.y * scaleY);
        }
        ofEndShape(true);
    }

    ofFill();
    ofColor bg = ofColor(0, 180);
    ofSetColor(bg);
    ofDrawRectangle(margin, margin + previewH + 6, previewW, 22);

    ofSetColor(255);
    std::string label = previewPatternVisible ? "Aruco pattern detected" : "Searching for pattern...";
    ofDrawBitmapString(label, margin + 6, margin + previewH + 22);
    ofPopStyle();

    if(depthEnabled) ofEnableDepthTest();
}

//--------------------------------------------------------------
void ofApp::applyPatternToFluid(const std::array<float,16>& strengths, float dt){
    float frameScale = (dt > 0.f) ? ofClamp(dt * 60.f, 0.5f, 1.5f) : 1.f;

    int activeCells = 0;
    float totalStrength = 0.f;

    for(int r = 0; r < 4; ++r){
        for(int c = 0; c < 4; ++c){
            float intensity = strengths[r * 4 + c];
            if(intensity <= patternIntensityThreshold){
                continue;
            }

            float u = (c + 0.5f) / 4.f; // 0..1 left→right
            float v = (r + 0.5f) / 4.f; // 0..1 top→bottom
            float xNorm = (u - 0.5f) * 2.f;     // -1..1
            float yNorm = (0.5f - v) * 2.f;     // -1..1 (flip Y so top is +)
            glm::vec2 pos = glm::vec2(xNorm, yNorm) * gridWorldHalfSize;

            float strength = intensity * patternImpulseGain * frameScale;
            injectImpulse(pos, strength);

            totalStrength += strength;
            ++activeCells;
        }
    }

    float now = ofGetElapsedTimef();
    if(activeCells > 0 && (now - lastPatternImpulseLogTime) >= patternImpulseLogCooldown){
        ofLogNotice("Fluid") << "Pattern impulses applied: cells=" << activeCells
                             << " totalStrength=" << totalStrength;
        lastPatternImpulseLogTime = now;
    }
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
