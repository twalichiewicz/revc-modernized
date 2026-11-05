// Regression for an upstream BVH hang: surface adjacency need not be connected.
#include <BulletSoftBody/btSoftBody.h>
#include <cstdio>
#include <vector>
int main() {
    btSoftBodyWorldInfo info;int failed=0;
    for(int faces:{0,1,2,3,5,16}) {
        std::vector<btVector3> x;std::vector<btScalar> mass(3*faces,1);
        for(int i=0;i<faces;++i)for(auto p:{btVector3(3.*i-.3,-.3,0),btVector3(3.*i+.3,-.3,0),btVector3(3.*i,.3,0)})x.push_back(p);
        btSoftBody body(&info,int(x.size()),x.data(),mass.data());
        for(int i=0;i<faces;++i)body.appendFace(3*i,3*i+1,3*i+2);
        body.initializeFaceTree();
        int hits=0,gaps=0;
        for(int i=0;i<faces;++i) {
            btSoftBody::sRayCast r;
            hits+=body.rayFaceTest({3.*i,0,1},{3.*i,0,-1},r);
            if(i+1<faces)gaps+=body.rayFaceTest({3.*i+1.5,0,1},{3.*i+1.5,0,-1},r);
        }
        bool pass=hits==faces&&gaps==0;failed+=!pass;
        std::printf("disconnected-face-tree faces=%d actual_face_hits=%d phantom_gap_hits=%d %s\n",faces,hits,gaps,pass?"PASS":"FAIL");std::fflush(stdout);
    }
    return failed?1:0;
}
