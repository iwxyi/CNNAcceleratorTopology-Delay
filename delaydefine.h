#ifndef DELAYDEFINE_H
#define DELAYDEFINE_H

#define Dly_Cube2RegFIFO 1  // 每个数据到RequestQueue里面的delay
#define Dly_inReqFIFO 0     // ReqFIFO中的delay
#define Dly_onPick 1        // ReqQueue里每个数据Pick去卷积的delay
#define Dly_inConv 1        // 在Conv中的delay
#define Dly_Conv2SndFIFO 1  // 卷积后进入SndFIFO的delay
#define Dly_Snd2NextPE 1    // Send至下一层PE的delay

#endif // DELAYDEFINE_H
