#include "ofApp.h"
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

    setupArucoTracker(0, arucoCamW, arucoCamH);
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
                                "q: toggle debug | +/- strength",
                                12, ofGetHeight()-32);
}

void ofApp::keyPressed(int key){
    if(key=='q' || key=='Q') showDebug = !showDebug;
    if(key=='+') arucoMaxStrength = std::min(1.5f, arucoMaxStrength + 0.05f);
    if(key=='-') arucoMaxStrength = std::max(0.05f, arucoMaxStrength - 0.05f);
}

void ofApp::mousePressed(int, int, int){}
void ofApp::mouseDragged(int, int, int){}
void ofApp::mouseReleased(int, int, int){}
void ofApp::mouseScrolled(int, int, float, float){}

// ============================ ArUco tracker ============================
void ofApp::setupArucoTracker(int deviceId, int w, int h){
    if(!useArucoTracker) return;

    arucoCamDeviceId = deviceId;
    arucoCamW = w; arucoCamH = h;

    auto devs = arucoCam.listDevices();
    arucoCam.setDeviceID(ofClamp(deviceId, 0, (int)devs.size()-1));
    arucoCam.setDesiredFrameRate(30);
    // DEPRECATION: initGrabber → setup
    arucoCam.setup(arucoCamW, arucoCamH);

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
    cv::aruco::detectMarkers(frameGray, arucoDict, markerCorners, markerIds, arucoParams, rejected);

    std::array<cv::Point2f,4> innerQuad;
    if(innerQuadFromMarkers(markerIds, markerCorners,
                            markerIdTL, markerIdTR, markerIdBR, markerIdBL, innerQuad))
    {
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

        // Copy into ofPixels explicitly (avoids setFromExternalPixels ambiguity)
        arucoWarpPix.setFromPixels(warped.data, arucoWarpW, arucoWarpH, OF_PIXELS_GRAY);
    } else {
        arucoHasH = false;
    }
}

void ofApp::computeGridDarknessFromWarp(){
    if(!useArucoTracker || !arucoHasH){
        std::fill(arucoCellDark.begin(), arucoCellDark.end(), 0.0f);
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
        }
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
    cv::aruco::detectMarkers(frameGray, arucoDict, markerCorners, markerIds, arucoParams, rejected);

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
