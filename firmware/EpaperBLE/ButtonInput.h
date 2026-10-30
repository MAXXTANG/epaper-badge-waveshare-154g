#pragma once
#include <stdint.h>

class ButtonInput {
 public:
  enum Event {None,ShortPress,LongPress};
  explicit ButtonInput(uint32_t holdMs):holdMs_(holdMs){}
  Event update(bool pressed,uint32_t now){
    if(pressed!=raw_){raw_=pressed;changed_=now;}
    if(raw_!=stable_&&uint32_t(now-changed_)>=35){
      stable_=raw_;
      if(stable_){started_=now;held_=false;}
      else if(!held_)return ShortPress;
    }
    if(stable_&&!held_&&uint32_t(now-started_)>=holdMs_){held_=true;return LongPress;}
    return None;
  }
 private:
  uint32_t holdMs_,changed_=0,started_=0;
  bool raw_=false,stable_=false,held_=false;
};
