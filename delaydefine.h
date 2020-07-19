#ifndef DELAYDEFINE_H
#define DELAYDEFINE_H

// 各种模块
#define PacketPointCount 2      // 每个req数据包带有几个点的数量
#define ReqQueue_MaxSize 24     // ReqQueue数据量上限
#define DatLatch_MaxSize 24     // 和Data数量上限（无效，没有空间也跟着req强行传进去）
#define Picker_FullBandwidth 5  // 1个clock进行pick的数据数量
#define ConvQueue_MaxSize 2     // 卷积核存储的数据包最大的大小

// 各种delay
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
