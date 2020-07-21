#ifndef DELAYDEFINE_H
#define DELAYDEFINE_H

/*// 各种模块
#define PacketPointCount 2       // 每个req数据包带有几个点的数量
#define ReqFIFO_MaxSize 24       // ReqQueue数据量上限
#define Picker_FullBandwidth 256 // 1个clock进行pick的数据数量
#define ConvFIFO_MaxSize 9       // 卷积核存储的数据包最大的大小：?*8B*数量
#define Switch_FullBandwidth 200 // Switch传输到下一层的：?*8B

// 各种delay
#define Dly_Map2RegFIFO 1   // 每个数据到ReqFIFO里面的delay
#define Dly_inReqFIFO 0     // ReqFIFO中的delay
#define Dly_onPick 1        // ReqQueue里每个数据Pick的delay
#define Dly_inConv 1        // 在Conv中的delay
#define Dly_Conv2SndFIFO 1  // 卷积后进入SndFIFO的delay
#define Dly_inSndFIFO 1     // 在SndFIFO的delay
#define Dly_SndPipe 1       // SndFIFO发送到Switch的delay
#define Dly_inSwitch 0      // 在switch中的delay
#define Dly_Switch2NextPE 1 // Send至下一层PE的delay*/

// 卷积属性
#define MAP_SIDE_MAX 224     // 特征图边长的最大
#define MAP_CHANNEL_DEFULT 3 // 特征图通道默认数量 RGB
#define KERNEL_SIDE 3        // 卷积核固定边长
#define KERNEL_MAX_COUNT 32  // 卷积核最大数量
#define MAX_LAYER 32         // 最多的层数（32还是128）

#endif // DELAYDEFINE_H
