#include "../tools/host/BodyAffine.h"
#include <cstdio>
#include <random>
using bodyaffine::Matrix;
std::array<float,3> Apply(const Matrix&m,std::array<float,3>p){return {{m[0]*p[0]+m[3]*p[1]+m[6]*p[2]+m[9],m[1]*p[0]+m[4]*p[1]+m[7]*p[2]+m[10],m[2]*p[0]+m[5]*p[1]+m[8]*p[2]+m[11]}};}
int main(){int failed=0;std::mt19937 random(37);std::uniform_real_distribution<float> value(-1,1);for(int n=0;n<2000;++n){Matrix m{{.3f+std::fabs(value(random)),value(random)*.2f,value(random)*.2f,value(random)*.2f,.3f+std::fabs(value(random)),value(random)*.2f,value(random)*.2f,value(random)*.2f,.3f+std::fabs(value(random)),value(random)*10,value(random)*10,value(random)*10}},inverse;std::array<float,3> point{{value(random)*5,value(random)*5,value(random)*5}};if(!bodyaffine::Inverse(m,inverse)){++failed;continue;}auto recovered=Apply(inverse,Apply(m,point));for(int axis=0;axis<3;++axis)failed+=std::fabs(recovered[axis]-point[axis])>.00003f;}
 Matrix singular{},unchanged{{1,0,0,0,1,0,0,0,1,2,3,4}},out=unchanged;failed+=bodyaffine::Inverse(singular,out)||out!=unchanged;singular=unchanged;singular[1]=NAN;failed+=bodyaffine::Inverse(singular,out)||out!=unchanged;std::printf("body affine roundtrip: %s\n",failed?"FAIL":"PASS");return failed?1:0;}
