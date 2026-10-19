#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using std::sinf;
using std::cosf;
template<class T> T _min(T a,T b) { return std::min(a,b); }
template<class T> T _max(T a,T b) { return std::max(a,b); }
float _abs(float a) { return fabsf(a); }
constexpr float EPS_S=0.00001f;
struct POINT { long x,y; };
enum ECameraStyle { csPlaneMove,cs3DArcBall,csFreeFly };
struct Fvector {
 float x=0,y=0,z=0;
 Fvector& set(float a,float b,float c) { x=a;y=b;z=c;return *this; }
 Fvector& sub(const Fvector&a,const Fvector&b){return set(a.x-b.x,a.y-b.y,a.z-b.z);}
 Fvector& mad(const Fvector&a,const Fvector&b,float c){return set(a.x+b.x*c,a.y+b.y*c,a.z+b.z*c);}
 Fvector& mul(float c){return set(x*c,y*c,z*c);}
 Fvector& div(float c){return mul(1/c);}
 float square_magnitude()const{return x*x+y*y+z*z;}
 float magnitude()const{return sqrtf(square_magnitude());}
 Fvector& normalize(){return div(magnitude());}
 Fvector& mknormal(const Fvector&a,const Fvector&b,const Fvector&c){
  Fvector u,v;u.sub(b,a);v.sub(c,a);set(u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x);return normalize();
 }
};
// Analytic fixture geometry replaces only the CDB backend. The production
// controller above it is extracted verbatim and compiled for every test run.
namespace CDB {
constexpr int OPT_ONLYNEAREST=4;
struct RESULT { Fvector verts[3];float range; };
struct MODEL { bool wall=false,ceiling=false,ledge=false;float step=0,slope=0; };
struct COLLIDER {
 std::vector<RESULT> results;
 void ray_options(int){}
 int r_count(){return int(results.size());}
 RESULT* r_begin(){return results.data();}
 void ray_query(const MODEL*m,const Fvector&s,const Fvector&d,float range){
  results.clear();
  auto hit=[&](float t,Fvector a,Fvector b,Fvector c){
   if(t>=0 && t<=range && (results.empty()||t<results[0].range))
    { results.clear();results.push_back({{a,b,c},t}); }
  };
  if(d.y-m->slope*d.x < -EPS_S){
   float ground=m->slope*s.x;
   if(m->step && s.x>=2) ground=m->step;
   float t=(ground-s.y)/(d.y-m->slope*d.x);
   if(!m->ledge || s.x+d.x*t<2)
    hit(t,{0,0,0},{0,0,1},{1,m->slope,0});
  }
  if(m->wall && d.x>EPS_S)
   hit((2-s.x)/d.x,{2,0,0},{2,1,0},{2,0,1});
  if(m->ceiling && d.y>EPS_S)
   hit((2.3f-s.y)/d.y,{0,2.3f,0},{1,2.3f,0},{0,2.3f,1});
 }
};
}
#include "controller-under-test.h"
static void check(bool b,const char*what){if(!b){printf("FAIL: %s\n",what);exit(1);}printf("PASS: %s\n",what);}
static CInGameMode* mode(){auto*m=new CInGameMode;m->position.set(0,1.7f,0);m->spawn=m->position;m->angles.set(0,0,0);m->grounded=true;return m;}
static void frames(CInGameMode&m,int count,bool run,float f,float s){for(int i=0;i<count;i++)m.Update(1.f/60,false,run,f,s);}
int main(){
 auto*m=mode();frames(*m,60,false,1,0);check(fabsf(m->position.z-3)<0.01f&&fabsf(m->position.y-1.7f)<0.01f,"walk 3 m/s and stay on ground");delete m;
 m=mode();frames(*m,60,true,1,0);check(fabsf(m->position.z-6)<0.01f,"run 6 m/s");delete m;
 m=mode();frames(*m,60,false,1,1);check(fabsf(hypotf(m->position.x,m->position.z)-3)<0.01f,"diagonal speed normalized");delete m;
 m=mode();m->angles.x=3.14159265f/2;frames(*m,60,false,1,0);check(m->position.x<-2.99f&&fabsf(m->position.z)<0.01f,"movement follows yaw");delete m;
 m=mode();m->collision.wall=true;frames(*m,120,true,1,1);check(m->position.x<=1.701f&&m->position.z>5,"wall blocks running while allowing slide");delete m;
 m=mode();m->Update(1.f/60,true,false,0,0);check(m->position.y>1.7f&&!m->grounded,"jump leaves ground");frames(*m,120,false,0,0);check(fabsf(m->position.y-1.7f)<0.01f&&m->grounded,"gravity lands jump");delete m;
 m=mode();m->collision.ceiling=true;m->Update(1.f/60,true,false,0,0);float peak=0;for(int i=0;i<120;i++){m->Update(1.f/60,false,false,0,0);peak=std::max(peak,m->position.y);}check(peak<2.3f&&m->grounded,"low ceiling blocks jump and lands");delete m;
 m=mode();m->collision.step=.25f;frames(*m,60,false,0,1);check(m->position.x>2.99f&&fabsf(m->position.y-1.95f)<.01f,"climb low step");delete m;
 m=mode();m->collision.slope=.3f;frames(*m,60,false,0,1);check(fabsf(m->position.y-2.6f)<.02f,"follow walkable slope");delete m;
 m=mode();m->collision.ledge=true;frames(*m,90,false,0,1);check(m->position.y<1&& !m->grounded,"fall after leaving ledge");frames(*m,400,false,0,0);check(fabsf(m->position.y-1.7f)<.01f&&m->grounded&&m->position.x==0,"respawn after deep fall");delete m;
 m=mode();m->Update(10,false,true,1,0);check(m->position.z<=.601f,"stall time is capped");delete m;
 puts("All 13 controller checks passed.");
}
