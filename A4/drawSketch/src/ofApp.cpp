#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
    prevMouse = ofPoint(mouseX, mouseY);
    bMousePressed = false;
}

//--------------------------------------------------------------
void ofApp::update(){

}

//--------------------------------------------------------------
void ofApp::draw(){
//  ## JITTERY LINES ##
//    for (int i = 0; i < line.size(); i++){
//        line[i].x += ofRandom(-1,1);
//        line[i].y += ofRandom(-1,1);
//    }
    
//  ## SMOOTHING LINE ##
//    line = line.getSmoothed(2.0);
    
//  ## RESAMPLE COUNT ##
//    ofPolyline newLine = line.getResampledByCount(50);
//    for (int i = 0; i < newLine.size(); i++){
//        ofDrawCircle(newLine[i].x, newLine[i].y, 10);
//    }
    
//  ## RESAMPLE SPACING ##
//    ofPolyline newLine = line.getResampledBySpacing(20);
//    for (int i = 0; i < newLine.size(); i++){
//         ofDrawCircle(newLine[i].x, newLine[i].y, 10);
//        }
    
    if (bMousePressed == true){
        line.addVertex(mouseX, mouseY);
    }
    
    line.draw();
    
    for (int i = 0; i < line.size(); i++){
        line[i] -= ofPoint(ofGetWidth()/2, ofGetHeight()/2);
    }
    
    ofMatrix4x4 mat;
    mat.makeIdentityMatrix();
    mat.rotate(1,0,1,0);
    
    for (int i = 0; i<line.size(); i++){
        ofPoint pt = line[i];
        pt = mat * pt;
        line[i] = pt;
    }
    
    for (int i = 0; i < line.size(); i++){
        line[i] += ofPoint(ofGetWidth()/2, ofGetHeight()/2);
    }
    
//  ## ./LINE TO MESH ##
//    ofMesh mesh;
//    mesh.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
    
// ## TANGENT LINE ON LINE ##
//    for (int i = 0; i < line.size(); i++){
//        int i_m_1 = i-1;
//        int i_p_1 = i+1;
//        if (i_m_1 < 0) i_m_1 = 0;
//        if (i_p_1 == line.size()) i_p_1 = line.size()-1;
//        ofPoint a = line[i_m_1];
//        ofPoint b = line[i];
//        ofPoint c = line[i_p_1];
//        ofPoint diff = (c-a).getNormalized();
//        diff = diff.getRotated(90, ofPoint(0,0,1));
//        ofDrawLine(b, b + diff*100);
//      ## ./LINE TO MESH ##
//      ## ././VELOCITY MOD ##
//        float vel = (c-a).length()*5;
//        mesh.addVertex(b + diff*vel);
//        mesh.addVertex(b - diff*vel);
//      ## ././MESH ##
//        mesh.addVertex(b + diff*10);
//        mesh.addVertex(b - diff*10);
//        mesh.addColor(ofColor::white);
//        mesh.addColor(ofColor::black);
//    }
//    mesh.draw();
}

//--------------------------------------------------------------
void ofApp::exit(){

}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
    
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){
    
}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){
    
//  ## REVERSE DRAWING ##
//    ofPoint diff = ofPoint(x,y) - prevMouse;
//    ofPoint pt = line[line.size()-1];
//    line.addVertex(pt + -diff);
//    prevMouse = ofPoint(x,y);
//  ## REVERSE DRAWING ON TRACK MOUSE ##
//    ofPoint diffToMouse = pt - ofPoint(x,y);
//    for (int i = 0; i < line.size(); i++){
//        line[i] -= diffToMouse;
//    }
    
    line.addVertex(x,y);
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){
    line.clear();
    line.addVertex(x,y);
    prevMouse = ofPoint(x,y);
    
    bMousePressed = true;
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){
    bMousePressed = false;
}

//--------------------------------------------------------------
void ofApp::mouseScrolled(int x, int y, float scrollX, float scrollY){

}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){

}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){

}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
