#include "../engine.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using namespace touchmouse;
struct Mock:Sink{
 bool targetValid=true,failMove=false,failDown=false,failUp=false;
 std::vector<std::string> events;
 bool valid(std::uintptr_t,Point)override{return targetValid;}
 bool move(Point)override{events.push_back("move");return !failMove;}
 bool button(bool d)override{events.push_back(d?"down":"up");return !(d&&failDown)&&!(!d&&failUp);}
 int count(const char* s){return std::count(events.begin(),events.end(),s);}
};
Settings enabled(int n=1){Settings c;c.enabled=true;c.clicks=n;return c;}
void tap(Engine&e,int t=0){assert(e.touchDown(1,{20,20},true,t));assert(e.touchUp({20,20},t));}
void run(Engine&e,int end=400){for(int t=0;t<=end;t+=10)e.tick(t);}
int main(){
 {Mock m;Engine e(m);e.configure(enabled());tap(e);e.tick(0);e.tick(20);assert(m.count("down")==1&&m.count("up")==0);e.tick(70);assert(m.count("up")==0);e.tick(80);assert(m.count("up")==1&&e.clicks==1);}
 {Mock m;Engine e(m);e.configure(enabled(2));tap(e);run(e);assert(m.count("down")==2&&m.count("up")==2&&e.clicks==2);}
 {Mock m;Engine e(m);assert(!e.touchDown(1,{},true,0));e.configure(enabled());assert(!e.touchDown(1,{},false,0));assert(m.events.empty());}
 {Mock m;Engine e(m);e.configure(enabled());tap(e);e.tick(0);m.targetValid=false;e.tick(20);assert(m.count("down")==0);}
 {Mock m;Engine e(m);e.configure(enabled());tap(e);e.tick(0);e.tick(20);m.targetValid=false;e.tick(30);assert(m.count("up")==1&&!e.ownsButton());}
 {Mock m;Engine e(m);e.configure(enabled());assert(e.touchDown(1,{},true,0));e.tick(0);e.tick(20);e.configure(Settings{});assert(m.count("up")==1);assert(e.touchUp({},40));assert(!e.touchUp({},50));}
 {Mock m;Engine e(m);e.configure(enabled());e.physicalDown();assert(!e.touchDown(1,{},true,0));e.physicalUp();tap(e);run(e);assert(e.clicks==1);}
 {Mock m;Engine e(m);e.configure(enabled());tap(e);e.tick(0);e.tick(20);e.physicalDown();e.tick(90);assert(m.count("up")==0&&!e.ownsButton());e.physicalUp();}
 {Mock m;Engine e(m);e.configure(enabled());tap(e);e.tick(1000);assert(m.count("down")==0&&e.dropped==1);}
 {Mock m;Engine e(m);e.configure(enabled());m.failDown=true;tap(e);run(e);assert(e.errors==1&&!e.ownsButton()&&m.count("up")==0);}
 {Mock m;Engine e(m);e.configure(enabled(2));assert(e.touchDown(1,{},true,0));e.tick(0);e.tick(20);e.touchMove({100,100});e.tick(50);assert(m.count("up")==0);e.touchUp({100,100},70);e.tick(80);run(e);assert(e.clicks==1);}
 {Mock m;Engine e(m);e.configure(enabled());e.touchDown(1,{},true,0);e.tick(0);e.tick(20);e.tick(4000);assert(!e.ownsButton()&&m.count("up")==1);}
 {Mock m;Engine e(m);e.configure(enabled());for(int i=0;i<8;i++)tap(e,i);run(e);assert(e.touches==8&&e.dropped==4&&m.count("down")==4);}
 {Mock m;Engine e(m);e.configure(enabled());tap(e);e.tick(0);e.tick(20);m.failUp=true;e.tick(80);assert(e.ownsButton());m.failUp=false;e.tick(90);assert(!e.ownsButton());}
 {Mock m;Engine e(m);e.configure(enabled());tap(e);e.tick(0);e.tick(20);m.failUp=true;e.cancel();tap(e,30);e.tick(40);e.tick(80);assert(m.count("down")==1&&e.ownsButton());m.failUp=false;e.tick(90);e.tick(110);assert(m.count("down")==2);e.tick(180);assert(!e.ownsButton());}
 {Mock m;Engine e(m);e.configure(enabled());bool pass=false;assert(e.nativeTouchDown(1,{},true,0,pass)&&pass);assert(e.touchUp({},0));e.tick(50);assert(m.count("down")==0&&m.count("up")==0);e.tick(60);assert(m.count("up")==1&&e.clicks==1);}
 {Mock m;Engine e(m);e.configure(enabled(2));bool pass=false;assert(e.nativeTouchDown(1,{},true,0,pass)&&pass);e.touchUp({},0);run(e);assert(m.count("down")==1&&m.count("up")==2&&e.clicks==2);}
 {Mock m;Engine e(m);e.configure(enabled());bool pass=false;assert(e.nativeTouchDown(1,{},true,0,pass)&&pass);e.touchUp({},0);pass=false;assert(e.nativeTouchDown(1,{},true,25,pass)&&!pass);e.touchUp({},25);run(e);assert(m.count("down")==1&&m.count("up")==2&&e.clicks==2);}
 {Mock m;Engine e(m);e.configure(enabled());bool pass=false;e.nativeTouchDown(1,{20,20},true,0,pass);e.touchMove({20,20});e.tick(20);assert(m.count("move")==0);e.touchUp({20,20},25);e.tick(60);assert(m.count("move")==0&&m.count("up")==1);}
 {Mock m;Engine e(m);e.configure(enabled(2));bool pass=false;e.nativeTouchDown(1,{},true,0,pass);assert(!e.touchMove({100,100}));e.tick(30);e.touchUp({100,100},40);run(e);assert(m.count("move")==0&&m.count("down")==0&&m.count("up")==1&&e.clicks==1);}
 assert(isTouch(0xff515780u)&&!isTouch(0xff515700u)&&!isTouch(0x1234u));
 assert(normalize(-1920,-1920,3840)==0&&normalize(1919,-1920,3840)==65535&&normalize(1,0,1)==0);
 std::cout<<"22 input lifecycle and coordinate tests passed\n";
}
