//Author of this code: Dr. Matthew Harvey
//A low jitter, low cost optical chopper
//To cite this article: Parinya Udommai et al 2020 Meas. Sci. Technol. 31 125903

#include "Arduino.h"
#include "MilliTimer.h"

MilliTimer::MilliTimer(){
  timeOut=1000000;
  start=millis();
  current=start;
}

MilliTimer::MilliTimer(unsigned long t){
  timeOut=t;
  start=millis();
  current=start;
}

void MilliTimer::init(unsigned long t){
  timeOut=t;
  start=millis();
  current=start;
}

unsigned long MilliTimer::elapsed(){
  current=millis();
  return current-start;
}

// bool MilliTimer::timedOut(){
//   if(elapsed() >= timeOut){
//     return true;
//   }
//   else{
//     return false;
//   }
// }

bool MilliTimer::timedOut(bool RESET){
  if(elapsed() >= timeOut){
    if(RESET) reset();
    return true;
  }
	else{
    return false;
  }
}

void MilliTimer::updateTimeOut(unsigned long t){
  timeOut=t;
}

void MilliTimer::reset(){
  start=millis();
  current=start;
}

unsigned long MilliTimer::getTimeOut(){
  return timeOut;
}
