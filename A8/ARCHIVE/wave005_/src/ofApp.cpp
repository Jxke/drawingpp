#include "ofApp.h"
#include <sstream>
#include <limits>
using namespace cv;

// ---------- small helpers ----------
static bool getMarkerCornerById(const std::vector<int>& ids,
                                const std::vector<std::vector<Point2f>>& corners,
                                int wantId, std::array<Point2f,4>& outCorners)
{
    for(size_t i=0;i<ids.size();++i){
        if(ids[i]==wantId){
            const auto& c = corners[i]; // order TL,TR,BR,BL
            outCorners = { c[0], c[1], c[2], c[3] };
            return true;
        }
    }
    return false;
}

static bool innerQuadFromMarkers(const std::vector<int>& ids,
                                 const std::vector<std::vector<Point2f>>& corners,
                                 int idTL, int idTR, int idBR, int idBL,
                                 std::array<Point2f,4>& outTLTRBRBL)
{
    std::array<Point2f,4> tlC, trC, brC, blC;
    if(!getMarkerCornerById(ids, corners, idTL, tlC)) return false;
    if(!getMarkerCornerById(ids, corners, idTR, trC)) return false;
    if(!getMarkerCornerById(ids, corners, idBR, brC)) return false;
    if(!getMarkerCornerById(ids, corners, idBL, blC)) return false;

    // choose the inner corners pointing into the grid
    outTLTRBRBL[0] = tlC[2]; // TL -> BR
    outTLTRBRBL[1] = trC[3]; // TR -> BL
    outTLTRBRBL[2] = brC[0]; // BR -> TL
    outTLTRBRBL[3] = blC[1]; // BL -> TR
    return true;
}

static bool innerQuadFromMarkersAuto(const std::vector<std::vector<Point2f>>& corners,
                                     const std::vector<int>& ids,
                                     std::array<Point2f,4>& outTLTRBRBL,
                                     std::array<int,4>& outIds)
{
    if(corners.size() < 4) return false;

    std::array<float,4> bestDist;
    std::array<int,4> bestIdx;
    bestDist.fill(std::numeric_limits<float>::max());
    bestIdx.fill(-1);
    outIds.fill(-1);

    std::vector<Point2f> centers(corners.size());
    float minCx = std::numeric_limits<float>::max();
    float maxCx = std::numeric_limits<float>::lowest();
    float minCy = std::numeric_limits<float>::max();
    float maxCy = std::numeric_limits<float>::lowest();

    for(size_t i=0;i<corners.size();++i){
        float cx = 0.0f, cy = 0.0f;
        for(const auto& p : corners[i]){
            cx += p.x;
            cy += p.y;
        }
        cx *= 0.25f;
        cy *= 0.25f;
        centers[i] = {cx, cy};
        minCx = std::min(minCx, cx);
        maxCx = std::max(maxCx, cx);
        minCy = std::min(minCy, cy);
        maxCy = std::max(maxCy, cy);
    }

    float midX = 0.5f * (minCx + maxCx);
    float midY = 0.5f * (minCy + maxCy);

    for(size_t i=0;i<centers.size();++i){
        const auto& center = centers[i];
        bool left = center.x <= midX;
        bool top  = center.y <= midY;
        int slot;
        if(top && left) slot = 0;        // TL
        else if(top && !left) slot = 1;  // TR
        else if(!top && !left) slot = 2; // BR
        else slot = 3;                   // BL

        Point2f target(left ? minCx : maxCx, top ? minCy : maxCy);
        float dx = center.x - target.x;
        float dy = center.y - target.y;
        float dist = dx*dx + dy*dy;

        if(dist < bestDist[slot]){
            bestDist[slot] = dist;
            bestIdx[slot] = static_cast<int>(i);
            outIds[slot] = ids.size() > i ? ids[i] : -1;
        }
    }

    for(int idx : bestIdx){
        if(idx < 0) return false;
    }

    outTLTRBRBL[0] = corners[bestIdx[0]][2];
    outTLTRBRBL[1] = corners[bestIdx[1]][3];
    outTLTRBRBL[2] = corners[bestIdx[2]][0];
    outTLTRBRBL[3] = corners[bestIdx[3]][1];
    return true;
}

// ============================ OF lifecycle ============================
void ofApp::setup(){
    ofSetVerticalSync(true);
    ofBackground(12);
    ofEnableDepthTest();

    cam.setDistance(1100);
    cam.setNearClip(1.0f);
    cam.setFarClip(10000.0f);

#ifndef DW_HAS_USER_INJECT_IMPULSE
    rebuildPlaneMesh();
#endif

    if(!setupArucoTracker(0, arucoCamW, arucoCamH)){
        ofLogWarning() << "ArUco tracker failed to initialize; disabling tracker.";
        useArucoTracker = false;
    }
}

void ofApp::update(){
    updateArucoTracker();
    computeGridDarknessFromWarp();
    applyPaperGridToFluid();

#ifndef DW_HAS_USER_INJECT_IMPULSE
    updatePlaneMesh(ofGetElapsedTimef());
#endif
}

void ofApp::draw(){
    ofEnableDepthTest();
    cam.begin();

#ifndef DW_HAS_USER_INJECT_IMPULSE
    ofSetColor(40);
    planeMesh.draw();
#else
    ofSetColor(100);
    ofDrawGridPlane(50.0f, 16, false); // harmless reference grid
#endif

    cam.end();
    ofDisableDepthTest();

    if(showDebug) drawArucoDebug(12, 12, 320);

    ofSetColor(255);
    ofDrawBitmapStringHighlight("Drawing Waves 005 — ArUco paper grid\n"
                                "q: toggle debug | +/- strength | v: switch camera",
                                12, ofGetHeight()-32);
}

void ofApp::keyPressed(int key){
    if(key=='q' || key=='Q') showDebug = !showDebug;
    if(key=='+') arucoMaxStrength = std::min(1.5f, arucoMaxStrength + 0.05f);
    if(key=='-') arucoMaxStrength = std::max(0.05f, arucoMaxStrength - 0.05f);
    if(key=='v' || key=='V') cycleArucoDevice(+1);
}

void ofApp::mousePressed(int, int, int){}
void ofApp::mouseDragged(int, int, int){}
void ofApp::mouseReleased(int, int, int){}
void ofApp::mouseScrolled(int, int, float, float){}

// ============================ ArUco tracker ============================
bool ofApp::setupArucoTracker(int deviceId, int w, int h){
    if(!useArucoTracker) return false;

    if(arucoCam.isInitialized()){
        arucoCam.close();
    }

    arucoDevices = arucoCam.listDevices();
    if(arucoDevices.empty()){
        ofLogWarning() << "No video devices available for ArUco tracker";
        return false;
    }

    int fallbackIdx = -1;
    int resolvedIdx = -1;
    for(int i=0; i<(int)arucoDevices.size(); ++i){
        const auto& dev = arucoDevices[i];
        if(!dev.bAvailable) continue;
        if(fallbackIdx == -1) fallbackIdx = i;
        if(deviceId >= 0 && dev.id == deviceId){
            resolvedIdx = i;
            break;
        }
    }
    if(resolvedIdx == -1){
        if(fallbackIdx == -1){
            ofLogWarning() << "No available video devices for ArUco tracker";
            return false;
        }
        resolvedIdx = fallbackIdx;
    }

    const auto& device = arucoDevices[resolvedIdx];

    auto startCamera = [&](int reqW, int reqH)->bool{
        arucoCam.setDeviceID(device.id);
        arucoCam.setDesiredFrameRate(30);
        bool success = arucoCam.setup(reqW, reqH);
        if(success){
            arucoCamW = static_cast<int>(arucoCam.getWidth());
            arucoCamH = static_cast<int>(arucoCam.getHeight());
        }
        return success;
    };

    bool ok = startCamera(w, h);
    if(!ok){
        ofLogWarning() << "Camera '" << device.deviceName << "' failed at "
                       << w << "x" << h << ", trying fallback resolution.";
        arucoCam.close();
        ok = startCamera(640, 480);
    }
    if(!ok){
        arucoCam.close();
        ofLogError() << "Failed to initialise camera '" << device.deviceName
                     << "' (id " << device.id << ")";
        arucoCam.close();
        return false;
    }

    arucoCamDeviceId = device.id;
    ofLogNotice() << "ArUco tracker using camera id " << device.id << " ("
                  << device.deviceName << ") at " << arucoCamW << "x" << arucoCamH;

    arucoColor.allocate(arucoCamW, arucoCamH);
    arucoGray.allocate(arucoCamW, arucoCamH);

    cv::aruco::Dictionary dictTmp = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
    arucoDict = cv::Ptr<cv::aruco::Dictionary>( new cv::aruco::Dictionary(dictTmp) );
    
    // Reasonable defaults (no ::create() needed)
    arucoParams = cv::Ptr<cv::aruco::DetectorParameters>(new cv::aruco::DetectorParameters());
    arucoParams->cornerRefinementMethod    = cv::aruco::CORNER_REFINE_SUBPIX;
    arucoParams->adaptiveThreshWinSizeMin  = 3;
    arucoParams->adaptiveThreshWinSizeMax  = 23;
    arucoParams->adaptiveThreshWinSizeStep = 10;

    arucoWarpPix.allocate(arucoWarpW, arucoWarpH, OF_PIXELS_GRAY);
    arucoCellDark.assign(arucoGridN*arucoGridN, 0.0f);
    lastArucoImpulseT = ofGetElapsedTimef();
    arucoHasH = false;
    arucoHadHPrev = false;
    arucoSlotIds.fill(-1);

    return true;
}

void ofApp::cycleArucoDevice(int delta){
    if(!useArucoTracker) return;

    arucoDevices = arucoCam.listDevices();
    std::vector<int> availableIds;
    availableIds.reserve(arucoDevices.size());
    for(const auto& dev : arucoDevices){
        if(dev.bAvailable) availableIds.push_back(dev.id);
    }

    if(availableIds.empty()){
        ofLogWarning() << "No available video devices to cycle";
        return;
    }
    if(availableIds.size() <= 1){
        ofLogNotice() << "Only one available camera; skipping cycle";
        return;
    }

    int prevId = arucoCamDeviceId;
    int currentIdx = 0;
    for(int i=0;i<(int)availableIds.size();++i){
        if(availableIds[i] == prevId){
            currentIdx = i;
            break;
        }
    }

    int nextIdx = (currentIdx + delta) % (int)availableIds.size();
    if(nextIdx < 0) nextIdx += (int)availableIds.size();
    int desiredId = availableIds[nextIdx];

    if(desiredId == prevId){
        return;
    }

    if(!setupArucoTracker(desiredId, arucoCamW, arucoCamH)){
        ofLogWarning() << "Switching to camera id " << desiredId << " failed; reverting to id " << prevId;
        if(prevId >= 0){
            setupArucoTracker(prevId, arucoCamW, arucoCamH);
        }
    }
}

void ofApp::updateArucoTracker(){
    if(!useArucoTracker || !arucoCam.isInitialized()) return;

    arucoCam.update();
    if(!arucoCam.isFrameNew()) return;

    // Copy camera -> color -> gray (no operator= on ofxOpenCv images)
    arucoColor.setFromPixels(arucoCam.getPixels());
    arucoGray.setFromPixels(arucoColor.getPixels());  // explicit copy

    // Convert legacy IplImage* to cv::Mat (copy=true)
    IplImage* ipl = arucoGray.getCvImage();
    cv::Mat frameGray = cv::cvarrToMat(ipl, true);

    std::vector<std::vector<cv::Point2f>> markerCorners;
    std::vector<int> markerIds;
    std::vector<std::vector<cv::Point2f>> rejected;

    // Pass the Ptr<DetectorParameters>
    cv::aruco::ArucoDetector detector(*arucoDict, *arucoParams);
    detector.detectMarkers(frameGray, markerCorners, markerIds, rejected);

    std::array<cv::Point2f,4> innerQuad;
    std::array<int,4> slotIdsLocal;
    slotIdsLocal.fill(-1);
    bool hadPrev = arucoHadHPrev;

    bool matched = innerQuadFromMarkers(markerIds, markerCorners,
                                        markerIdTL, markerIdTR, markerIdBR, markerIdBL, innerQuad);
    if(matched){
        slotIdsLocal = {markerIdTL, markerIdTR, markerIdBR, markerIdBL};
    } else {
        matched = innerQuadFromMarkersAuto(markerCorners, markerIds, innerQuad, slotIdsLocal);
    }

    if(matched){
        cv::Point2f dstPts[4] = {
            {0.0f, 0.0f},
            {(float)arucoWarpW-1, 0.0f},
            {(float)arucoWarpW-1, (float)arucoWarpH-1},
            {0.0f, (float)arucoWarpH-1}
        };
        arucoH = cv::getPerspectiveTransform(innerQuad.data(), dstPts);
        arucoHasH = true;

        cv::Mat warped;
        cv::warpPerspective(frameGray, warped, arucoH,
                            cv::Size(arucoWarpW, arucoWarpH),
                            cv::INTER_LINEAR, cv::BORDER_REPLICATE);

        arucoWarpPix.setFromPixels(warped.data, arucoWarpW, arucoWarpH, OF_PIXELS_GRAY);
        arucoSlotIds = slotIdsLocal;

        if(!hadPrev){
            std::ostringstream ss;
            ss << "[ArUco] Tracker LOCKED. Visible IDs:";
            if(!markerIds.empty()){
                for(int id : markerIds) ss << " " << id;
            } else {
                ss << " (none)";
            }
            ss << " | warp " << arucoWarpW << "x" << arucoWarpH;
            ofLogNotice() << ss.str();

            const char* slotNames[4] = {"TL","TR","BR","BL"};
            std::array<int,4> expected = {markerIdTL, markerIdTR, markerIdBR, markerIdBL};
            for(int i=0;i<4;++i){
                int seenId = slotIdsLocal[i];
                if(seenId == -1){
                    ofLogWarning() << "[ArUco] " << slotNames[i] << " marker missing.";
                } else if(seenId != expected[i]){
                    ofLogWarning() << "[ArUco] " << slotNames[i] << " marker expected "
                                   << expected[i] << " but saw " << seenId;
                }
            }
        }
    } else {
        arucoHasH = false;
        arucoSlotIds.fill(-1);
        if(hadPrev){
            ofLogNotice() << "[ArUco] Tracker LOST homography.";
        }
    }
    arucoHadHPrev = arucoHasH;
}

void ofApp::computeGridDarknessFromWarp(){
    if(!useArucoTracker || !arucoHasH){
        std::fill(arucoCellDark.begin(), arucoCellDark.end(), 0.0f);
        if(useArucoTracker){
            static float lastLog = 0.0f;
            float now = ofGetElapsedTimef();
            if(now - lastLog > 2.0f){
                ofLogNotice() << "[ArUco] Homography missing; impulses suppressed.";
                lastLog = now;
            }
        }
        return;
    }
    std::fill(arucoCellDark.begin(), arucoCellDark.end(), 0.0f);
    std::vector<int> cnt(arucoGridN*arucoGridN, 0);

    const unsigned char* pix = arucoWarpPix.getData();
    int stride = 2; // speed
    for(int y=0; y<arucoWarpH; y+=stride){
        int cj = std::max(0, std::min(arucoGridN-1, (y * arucoGridN) / arucoWarpH));
        for(int x=0; x<arucoWarpW; x+=stride){
            int ci = std::max(0, std::min(arucoGridN-1, (x * arucoGridN) / arucoWarpW));
            int k = cj*arucoGridN + ci;

            unsigned char g = pix[y * arucoWarpW + x];
            float dark = 1.0f - (g / 255.0f);
            arucoCellDark[k] += dark;
            cnt[k] += 1;
        }
    }
    for(int k=0;k<arucoGridN*arucoGridN;++k){
        if(cnt[k]>0) arucoCellDark[k] /= (float)cnt[k];
    }
}

glm::vec2 ofApp::arucoCellCenterWorld(int ci, int cj) const {
    float span = planeHalfSpan * arucoCoverage;
    float minX = -span, maxX = +span;
    float minY = -span, maxY = +span;
    float u = (ci + 0.5f) / float(arucoGridN);
    float v = (cj + 0.5f) / float(arucoGridN);
    return { ofLerp(minX, maxX, u), ofLerp(minY, maxY, v) };
}

void ofApp::applyPaperGridToFluid(){
    if(!useArucoTracker || !arucoHasH) return;

    float now = ofGetElapsedTimef();
    float minDt = 1.0f / std::max(1.0f, arucoImpulseHz);
    if(now - lastArucoImpulseT < minDt) return;
    lastArucoImpulseT = now;

    int firedCells = 0;
    float maxStrength = 0.0f;

    for(int cj=0; cj<arucoGridN; ++cj){
        for(int ci=0; ci<arucoGridN; ++ci){
            int k = cj*arucoGridN + ci;
            float d = ofClamp(arucoCellDark[k], 0.0f, 1.0f);
            if(d <= arucoDarkEps) continue;

            float strength = ofMap(d, 0.0f, 1.0f, arucoMinStrength, arucoMaxStrength, true);
            glm::vec2 world = arucoCellCenterWorld(ci, cj);

            float jx = ofRandomf() * (planeHalfSpan/float(arucoGridN)) * 0.06f;
            float jy = ofRandomf() * (planeHalfSpan/float(arucoGridN)) * 0.06f;

            injectImpulse({world.x + jx, world.y + jy}, strength);
            ++firedCells;
            maxStrength = std::max(maxStrength, strength);
        }
    }

    if(firedCells > 0 && (now - arucoLastImpulseLog) > 0.5f){
        ofLogNotice() << "[ArUco] Injected " << firedCells << " impulses (max strength "
                       << maxStrength << ")";
        arucoLastImpulseLog = now;
    }
}

void ofApp::drawArucoDebug(int x, int y, int w){
    if(!useArucoTracker || !arucoCam.isInitialized()) return;

    int h = int(std::round(w * (arucoCamH/float(arucoCamW))));
    ofPushStyle();
    ofSetColor(255);
    arucoCam.draw(x, y, w, h);

    // Overlay IDs & quads (detect again on the grayscale frame we already have)
    IplImage* ipl = arucoGray.getCvImage();
    Mat frameGray = cv::cvarrToMat(ipl, true);

    std::vector<std::vector<Point2f>> markerCorners;
    std::vector<int> markerIds; std::vector<std::vector<Point2f>> rejected;
    cv::aruco::ArucoDetector detector(*arucoDict, *arucoParams);
    detector.detectMarkers(frameGray, markerCorners, markerIds, rejected);

    for(size_t i=0;i<markerCorners.size();++i){
        const auto& c = markerCorners[i];
        ofSetColor(0,255,0);
        for(int e=0;e<4;++e){
            const Point2f& a = c[e];
            const Point2f& b = c[(e+1)%4];
            float ax = x + (a.x/arucoCamW)*w, ay = y + (a.y/arucoCamH)*h;
            float bx = x + (b.x/arucoCamW)*w, by = y + (b.y/arucoCamH)*h;
            ofDrawLine(ax, ay, bx, by);
        }
        float cx = 0.25f*(c[0].x + c[1].x + c[2].x + c[3].x);
        float cy = 0.25f*(c[0].y + c[1].y + c[2].y + c[3].y);
        cx = x + (cx/arucoCamW)*w; cy = y + (cy/arucoCamH)*h;
        ofSetColor(255,255,0);
        ofDrawBitmapStringHighlight("ID " + ofToString(markerIds[i]), cx-10, cy-6);
    }

    if(arucoHasH){
        int wx = x, wy = y + h + 8;
        int ww = w, wh = (arucoWarpH * ww) / arucoWarpW;
        ofImage tmp; tmp.setFromPixels(arucoWarpPix); tmp.draw(wx, wy, ww, wh);

        ofNoFill();
        ofSetColor(255,255,255,180);
        for(int i=1;i<arucoGridN;++i){
            float gx = wx + (ww * i / float(arucoGridN));
            float gy = wy + (wh * i / float(arucoGridN));
            ofDrawLine(gx, wy, gx, wy+wh);
            ofDrawLine(wx, gy, wx+ww, gy);
        }
        for(int cj=0;cj<arucoGridN;++cj){
            for(int ci=0;ci<arucoGridN;++ci){
                int k = cj*arucoGridN + ci;
                float d = ofClamp(arucoCellDark[k], 0.f, 1.f);
                ofSetColor(0,0,0, int(d*150));
                float cx = wx + (ww * ci / float(arucoGridN));
                float cy = wy + (wh * cj / float(arucoGridN));
                float cw = ww / float(arucoGridN);
                float ch = wh / float(arucoGridN);
                ofDrawRectangle(cx, cy, cw, ch);
            }
        }
    }
    ofPopStyle();
}

// ============================ fallback ripple demo ====================
#ifndef DW_HAS_USER_INJECT_IMPULSE
void ofApp::rebuildPlaneMesh(){
    planeMesh.clear();
    planeMesh.setMode(OF_PRIMITIVE_TRIANGLES);

    const float step = (planeHalfSpan*2.0f) / (planeRes-1);
    std::vector<std::vector<int>> vid(planeRes, std::vector<int>(planeRes, -1));

    for(int j=0;j<planeRes;++j){
        for(int i=0;i<planeRes;++i){
            float x = -planeHalfSpan + i*step;
            float y = -planeHalfSpan + j*step;
            planeMesh.addVertex({x, y, 0.0f});
            planeMesh.addNormal({0,0,1});
            planeMesh.addColor(ofFloatColor(0.8f));
            vid[j][i] = j*planeRes + i;
        }
    }
    for(int j=0;j<planeRes-1;++j){
        for(int i=0;i<planeRes-1;++i){
            int a = vid[j][i];
            int b = vid[j][i+1];
            int c = vid[j+1][i];
            int d = vid[j+1][i+1];
            planeMesh.addIndex(a); planeMesh.addIndex(b); planeMesh.addIndex(c);
            planeMesh.addIndex(b); planeMesh.addIndex(d); planeMesh.addIndex(c);
        }
    }
}

void ofApp::injectImpulse(const glm::vec2& worldXY, float strength){
    ripples.push_back({ worldXY, ofGetElapsedTimef(), strength });
    while(ripples.size() > 256) ripples.erase(ripples.begin());
}

void ofApp::updatePlaneMesh(float time){
    auto& v = planeMesh.getVertices();
    for(auto& p : v) p.z = 0.0f;

    for(const auto& r : ripples){
        float age = time - r.t0;
        if(age > 3.0f) continue;
        float k = 0.035f;
        float w = 8.0f;
        float decay = ofClamp(1.0f - age/3.0f, 0.0f, 1.0f);
        for(auto& p : v){
            glm::vec2 q(p.x, p.y);
            float dist = glm::length(q - r.p);
            p.z += r.s * decay * sinf(k*dist - w*age) * expf(-0.0035f*dist);
        }
    }
    // (normal recompute removed for OF 0.12.x compatibility)
}
#endif
