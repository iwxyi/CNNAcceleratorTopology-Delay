#ifndef DELAYDEFINE_H
#define DELAYDEFINE_H

#define Dly_Map2RegFIFO 1   // 每个数据到ReqFIFO里面的delay
#define Dly_inReqFIFO 0     // ReqFIFO中的delay
#define Dly_onPick 1        // ReqQueue里每个数据Pick去卷积的delay
#define Dly_inConv 1        // 在Conv中的delay
#define Dly_Conv2SndFIFO 1  // 卷积后进入SndFIFO的delay
#define Dly_inSndFIFO 1     // 在SndFIFO的delay
#define Dly_Snd2Switch 1    // 发送到Switch的delay
#define Dly_inSwitch 1      // 在switch中的delay
#define Dly_Switch2NextPE 1 // Send至下一层PE的delay

#endif // DELAYDEFINE_H
