//A low jitter, low cost optical chopper
//To cite this article: Parinya Udommai et al 2020 Meas. Sci. Technol. 31 125903

#include <arduino.h>
////////////////////////////////////////
//SERIAL
uint32_t baudrate = 250000;
#include "SerialChecker.h"
SerialChecker sc;
int serialLength;
//INTERUPT PINS
const int refPin = 33;
const int chopPin = 32;
//REF AND CHOP VARIABLES
volatile int32_t refT, prevRefT, chopT, prevChopT;
volatile int32_t refPeriod, chopPeriod, prevChopPeriod;
volatile bool updateFlag0, updateFlag;
int32_t phaseDiff, prevPhaseDiff;
float ConversionFactor;
//PID VARIABLES (Using 2*8-bit on board DAC)
const int dacCh1 = 25;		//Fine control
const int dacCh2 = 26;		//Course control
int32_t dacInput =20000;	//Initial dacInput out of 2^16
uint32_t DA = 65535;
int16_t RangeFreqDiff1 = 1;     //in Hz
int16_t RangeFreqDiff2 = 3;	//in Hz
int16_t phaseOffset = 0;
int N = 1;
int switchSign = 1;
float preK = 1;
float Kp = preK*10;
float Ki = preK*200;
float Kd0 = 0;			//Initial Kd
float Kd = preK*20000;
bool PhaseLockMode = false;
//MOVING AVERAGE
#include "MovingAverage.h"
const uint8_t numberSlot = 10;
const uint8_t windowSize = numberSlot;
MovingAverage <int32_t> avRefPeriod(windowSize,true);
MovingAverage <int32_t> avChopPeriod(windowSize,true);
float aveRefPeriod, aveChopPeriod, periodDiff;
float refFreq, chopFreq, freqDiff;
int8_t FreqLockGain = 1;
int32_t PhaseShift, SUMphaseDiff, ChangePhaseDiff;
uint16_t CPDlimit = 30;  //degree unit

uint16_t Count = 0;

void setup() {
	//SERIAL
	Serial.begin(baudrate);
	Serial.println("Chopper by ESP32");
    sc.init();
    sc.enableACKNAK('%', '*');
    sc.enableSTX(false, '£');
	//INTERUPT PINS
	pinMode(refPin, INPUT);
	pinMode(chopPin, INPUT);
	attachInterrupt(digitalPinToInterrupt(refPin), refChange, FALLING);
	attachInterrupt(digitalPinToInterrupt(chopPin), chopChange, RISING);
	//ON BOARD DAC'S
	pinMode(dacCh1,OUTPUT);
	pinMode(dacCh2,OUTPUT);
	set16bitVoltage(dacInput);
	//set16bitVoltage(DA);
}

void loop() {
	//CHECK SERIAL
	serialLength = sc.check();
    if(serialLength){
        if(sc.contains("Kp"))	{Kp = sc.toFloat();}
        else if(sc.contains("Ki"))	{Ki = sc.toFloat();}
        else if(sc.contains("Kd"))	{Kd = sc.toFloat();}
		else if(sc.contains("CP"))	{CPDlimit = sc.toInt16();}
		else if(sc.contains("CT"))	{Count = 1;}
		else if(sc.contains("DA"))	{
			DA = sc.toInt16();
			set16bitVoltage(DA);
		}
        else if(sc.contains("OF"))	{
			phaseOffset = int(sc.toFloat())%360;
			N = 1;
			detachInterrupt(digitalPinToInterrupt(refPin));
			attachInterrupt(digitalPinToInterrupt(refPin), refChange, FALLING);
			if (phaseOffset >180) {phaseOffset = phaseOffset - 360;}
			else if (phaseOffset <-180) {phaseOffset = phaseOffset + 360;}
			if (abs(phaseOffset) > 150){ //from(-180,180) to (0,360)degrees 
				phaseOffset = (phaseOffset+360)%360;
				N = 0;
				detachInterrupt(digitalPinToInterrupt(refPin));
				attachInterrupt(digitalPinToInterrupt(refPin), refChange, RISING);
			}
		}
    }
  //LOCKING ALGORITHM
  if (updateFlag) {
	//VARIABLES FOR FREQUENCY LOCK MODE (IN us UNITS)
	aveRefPeriod = avRefPeriod.updateFloat(refPeriod);
    aveChopPeriod = avChopPeriod.updateFloat(chopPeriod);
	//periodDiff = aveRefPeriod - aveChopPeriod;
	refFreq = 1000000.0/aveRefPeriod;		//Hz
	chopFreq = 1000000.0/aveChopPeriod;		//Hz
	freqDiff = chopFreq - refFreq;
    //VARIABLES FOR PHASE LOCK MODE (IN TICK UNIT, 1us = 1tick) 
		ConversionFactor = 360.0/float(refPeriod);	//us to degree conversion
	phaseDiff = float((chopT - refT)-(int(aveRefPeriod)>>N))*ConversionFactor; //in degrees		
	PhaseShift = phaseDiff - phaseOffset;			//works with Kp
	SUMphaseDiff = SUMphaseDiff + (PhaseShift);		//works with Ki
	ChangePhaseDiff = phaseDiff - prevPhaseDiff;		//works with Kd
	prevPhaseDiff = phaseDiff;
    //Go to Frequency lock mode if the freqDiff is out of range defined.
	if (freqDiff > RangeFreqDiff2 || freqDiff < - RangeFreqDiff2) {PhaseLockMode = false;}
	// FREQUENCY LOCK MODE
    if ((freqDiff > RangeFreqDiff1 || freqDiff < - RangeFreqDiff1) && PhaseLockMode == false) {
        //dacInput = dacInput + (periodDiff/abs(periodDiff));
		if (abs(freqDiff)>100){ FreqLockGain = 10;}	//Make the locking Gain larger if the freqDiff is large.
		else {
			FreqLockGain = 1;			//For small freqDiff, locking Gain is 1.
		}
		dacInput = dacInput + FreqLockGain*(freqDiff/abs(freqDiff));
		Kd0 = 0;		//Reset Kd
		SUMphaseDiff = 0;	//Reset Integral term    
    }
    // PHASE LOCK MODE      
    else {
        PhaseLockMode = true;
        Kd0++;
        if (Kd0 > Kd){Kd0 = Kd;}	//set upper limit to Kd0
        //Push chop Rising edge to the further refRising edge
		if (PhaseShift*freqDiff > 0) {switchSign = -1;} 
		//PID feedback
		if (ChangePhaseDiff > CPDlimit) {ChangePhaseDiff = CPDlimit;}
		else if (ChangePhaseDiff < -CPDlimit) {ChangePhaseDiff = -CPDlimit;}
        dacInput = dacInput - Kp*1e-3*float(switchSign*PhaseShift) - Ki*1e-8*float(SUMphaseDiff) - Kd0*1e-3*float(ChangePhaseDiff);
        switchSign = 1;	//Reset the switchSign
    }
	if (dacInput > 65535) {dacInput = 65535;}
	else if (dacInput < 0) {dacInput =0;}
	set16bitVoltage(dacInput);
	//updateFlag0 = false;
	updateFlag = false;
	//PRINT SOMETHING
	//Serial.println(PhaseLockMode);
  }
}
void set16bitVoltage(uint32_t _dacInput) {
	uint16_t _Course = (_dacInput>>8);
	uint16_t _Fine = _dacInput - (_Course<<8);
	dacWrite(dacCh1,_Fine);
	dacWrite(dacCh2,_Course);
}	
void refChange(){
	refT = micros();
	refPeriod = refT - prevRefT;
	prevRefT = refT;
	//updateFlag0 = true;
}
void chopChange(){
	chopT = micros();
	prevChopPeriod = chopPeriod;
	chopPeriod = chopT - prevChopT;
	prevChopT = chopT;
	updateFlag = true;
}
