#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>

namespace touchmouse {
struct Point { int x=0,y=0; };
struct Settings { bool enabled=false; int holdMs=60; int leadMs=18; int gapMs=90; int clicks=1; };
struct Sink {
 virtual ~Sink()=default;
 virtual bool valid(std::uintptr_t target,Point p)=0;
 virtual bool move(Point p)=0;
 virtual bool button(bool down)=0;
};
class Engine {
 struct Job { std::uint64_t id; std::uintptr_t target; Point point,start; std::uint64_t created; bool ended=false,dragged=false; int remaining=1; bool native=false; };
 enum class Phase { Idle, Lead, Down, Gap };
 Sink& sink;
 std::deque<Job> jobs;
 Settings cfg;
 Phase phase=Phase::Idle;
 std::uint64_t due=0, downAt=0, serial=0, capture=0, captureAt=0;
 bool drainUp=false, physical=false, owns=false, moveDirty=false;
 Job* captured(){ for(auto& j:jobs) if(j.id==capture)return &j; return nullptr; }
 void release(){ if(owns){if(sink.button(false))owns=false;else ++errors;} }
 void discardFront(){release();if(!jobs.empty())jobs.pop_front();phase=Phase::Idle;moveDirty=false;}
public:
 std::uint64_t touches=0,clicks=0,dropped=0,errors=0;
 explicit Engine(Sink& s):sink(s){}
 void configure(Settings s){cancel();cfg=s;cfg.holdMs=std::clamp(cfg.holdMs,20,250);cfg.clicks=std::clamp(cfg.clicks,1,2);}
 void cancel(){release();jobs.clear();phase=Phase::Idle;capture=0;moveDirty=false;/* Drain an already swallowed primary touch UP. */}
 void physicalDown(){physical=true;/* Real mouse takes ownership: never release its drag. */owns=false;jobs.clear();phase=Phase::Idle;capture=0;moveDirty=false;}
 void physicalUp(){physical=false;}
 bool touchDown(std::uintptr_t target,Point p,bool eligible,std::uint64_t now){
  if(drainUp&&capture){++dropped;return true;}
  drainUp=false;
  if(!cfg.enabled||!eligible||physical)return false;
  ++touches;drainUp=true;captureAt=now;
  if(jobs.size()>=4){++dropped;capture=0;return true;}
  capture=++serial;jobs.push_back({capture,target,p,p,now,false,false,cfg.clicks});return true;
 }
 bool nativeTouchDown(std::uintptr_t target,Point p,bool eligible,std::uint64_t now,bool& pass){
  pass=false;bool handled=touchDown(target,p,eligible,now);
  if(handled&&capture&&jobs.size()==1&&phase==Phase::Idle&&!owns){
   // Let Windows deliver the primary DOWN; suppress and delay its UP.
   // Blocking the primary DOWN can cause Windows to discard a batched UP.
   jobs.front().native=true;phase=Phase::Down;owns=true;downAt=now;pass=true;
  }
  return handled;
 }
 bool touchMove(Point p){
  if(!drainUp)return false;
  if(auto* j=captured()){
   bool pass=j->native&&phase==Phase::Down&&j->id==jobs.front().id;
   if(j->point.x==p.x&&j->point.y==p.y)return !pass;
   j->point=p;
   auto dx=static_cast<long long>(p.x)-j->start.x,dy=static_cast<long long>(p.y)-j->start.y;
   if(dx*dx+dy*dy>24*24){j->dragged=true;j->remaining=1;}
   if(!pass)moveDirty=true;
   return !pass;
  }
  return true;
 }
 bool touchUp(Point p,std::uint64_t){
  if(!drainUp)return false;
  if(auto* j=captured()){if(!j->native&&(j->point.x!=p.x||j->point.y!=p.y))moveDirty=true;j->point=p;j->ended=true;}
  capture=0;drainUp=false;return true;
 }
 void tick(std::uint64_t now){
  // Lost contact/late queues must never become a delayed click on another window.
  if(drainUp&&now-captureAt>3000){capture=0;drainUp=false;cancel();++dropped;}
  // A failed release must be retried before any subsequent press.
  if(owns&&phase!=Phase::Down){release();if(owns)return;}
  if(jobs.empty()){if(owns)release();return;}
  auto& j=jobs.front();
  if(physical||!cfg.enabled||!sink.valid(j.target,j.point)||now-j.created>3000){discardFront();++dropped;return;}
  if(phase!=Phase::Down&&now-j.created>600){discardFront();++dropped;return;}
  switch(phase){
   case Phase::Idle:
    if(!sink.move(j.point)){++errors;discardFront();return;}
    moveDirty=false;due=now+cfg.leadMs;phase=Phase::Lead;break;
   case Phase::Lead:
    if(now<due)break;
    if(!sink.move(j.point)||!sink.button(true)){++errors;discardFront();return;}
    owns=true;downAt=now;moveDirty=false;phase=Phase::Down;break;
   case Phase::Down:
    if(moveDirty){if(!sink.move(j.point)){++errors;discardFront();return;}moveDirty=false;}
    if(j.ended&&now-downAt>=static_cast<std::uint64_t>(cfg.holdMs)){
     release();if(owns)return;++clicks;
     if(--j.remaining>0&&!j.dragged){due=now+std::max(cfg.gapMs,cfg.holdMs);phase=Phase::Gap;}
     else {jobs.pop_front();phase=Phase::Idle;}
    }
    break;
   case Phase::Gap:
    if(now>=due){if(!sink.move(j.point)){++errors;discardFront();return;}due=now+cfg.leadMs;phase=Phase::Lead;}
    break;
  }
 }
 bool busy()const{return !jobs.empty()||drainUp||owns;}
 bool ownsButton()const{return owns;}
 bool physicalHeld()const{return physical;}
 bool contactActive()const{return drainUp&&capture!=0;}
};
inline bool isTouch(std::uintptr_t extra){return (extra&0xffffff00u)==0xff515700u&&(extra&0x80u)!=0;}
inline long normalize(int v,int origin,int extent){if(extent<=1)return 0;return static_cast<long>(std::clamp((static_cast<long long>(v)-origin)*65535/(extent-1),0LL,65535LL));}
}
