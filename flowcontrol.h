/**
  * �������̿��Ƶ���
  */

/**
  * TODO:
  * - С�����кܶ���ظ�Ϊ3�Σ�
  * - С����ÿ���㶼Ҫ�ֳ�8(kernel)�ݣ�����ʱ�ֻ���pickʱ�֣�
  */

#ifndef FLOWCONTR_H
#define FLOWCONTR_H

#include <list>
#include <windows.h>
#include "datapacket.h"
#include "layerthread.h"
#include "delaydefine.h"

#define PacketPointCount 2      // ÿ��req���м����������
#define ReqQueue_MaxSize 24     // ReqQueue����������
#define DatLatch_MaxSize 24     // ������Ҫһ����С
#define Picker_FullBandwidth 5  // 1��clock����pick����������bandwidth
#define ConvPoints_MaxSize 1600 // Ҫ�����ĵ�ļ����������ޣ�Ҫ����Ļ�������Ҫ3*3*3*32
#define ConvQueue_MaxSize 2    // ��������洢�����Ĵ�С

#define DEB if(1) printf
typedef int ClockType;
typedef std::vector<DataPacket*> FIFO;


// ��һ��ʼ�����ھ�����clock
ClockType global_clock = 0;
// ĳһ��clock�Ƿ������������䣬��������������ж�������������
// ʹ�ô�flag������̻߳����޷�ģ����̵߳Ķ�����ͬ����������
bool has_transfered = false;
int picker_bandwdith = Picker_FullBandwidth; // pick�����bandwidth
int picker_tagret = 0; // picker��һ��pick��Ŀ�꣬0~layer_kernel-1��������У�������

// ==================== ���ֶ��� ===================
FIFO StartQueue; // ����ͼ��ÿһ�����ɺ���ܶ���
FIFO ReqQueue;   // ����ͼ��ÿһ��req�Ķ��У�tag��data��ͬ
//FIFO DatLatch;   // ����ͼ��ÿһ��data�Ķ��У�tag��req��ͬ
FIFO PickQueue; // pick�����delay�Ķ���
//PointVec ConvPoints[KERNEL_MAX_COUNT]; // �������ݵȴ�����
FIFO ConvQueue[KERNEL_MAX_COUNT]; // ÿ�������������
FIFO Conv2SndFIFO; // Conv => SndFIFO
FIFO SndQueue;   // �ϲ�������ݶ��У����͵���һ��

// ==================== ͼ����� ===================
FeatureMap* feature_map = NULL; // ��ǰ����ͼ
std::vector<Kernel*> kernels;   // ����������


// ==================== ����ͼ���� ===================
/**
 * ������ͼ�ָ�ɷǳ��ǳ�С���������ݰ�
 * ����ÿ��С�����ݰ����ظ�kernel������
 * ��˳�����������������ܷ����ˣ����뵽�������棬׼������
 * @param map    Ҫ����������ͼ
 * @param kernel ���ﲻ��Ҫ��ӣ�ֻ�Ƿָ�
 * @param queue  �洢������ݰ��Ķ���
 */
void splitMap2Queue(FeatureMap* map, Kernel* kernel, FIFO& queue)
{
    // ������ͼ�ָ��С�����
    INT8*** m = map->map;
    int side = map->side - kernel->side + 1;
    PointVec points;
    for (int y = 0; y < side; y++)
    {
        for (int x = 0; x < side; x++)
        {
            for (int z = 0; z < kernel->channel; z++)
            {
                points.push_back(PointBean(y, x, z, m[y][x][z]));
            }
        }
    }

    // ��ÿ���������ܹ����͵����ݰ�
    // �洢��Ԥ�����͵Ķ�����
    unsigned int size = points.size();
    for (unsigned int i = 0; i < size; )
    {
        // ������ݰ����׸�������꣬�������
        int y = points[i].y;
        int x = points[i].x;
        int z = points[i].z;

        // ÿ�����ݰ�����������
        PointVec vec;
        for (int j = 0; j < PacketPointCount && i < size; j++)
        {
            vec.push_back(points[i++]);
        }

        // ������ǰ��kernel����������Kernel�����ⶼһģһ�������ݰ�
        // ��Ϊͬһ����ᱻ�������ͬ�ľ����˶�ȡ
        for (int knl = 0; knl < layer_kernel; knl++)
        {
            DataPacket* packet = new DataPacket(m[y][x][z]);
            packet->ImgID = (INT8)z;
            packet->CubeID = (INT8)y;
            packet->SubID = (INT8)x;
            // Tag��ʱΪ����IDƴ�ӣ�ȷ��ÿ��packet��Tag������ͬ
            packet->Tag = (packet->ImgID << 16)
                    + (packet->CubeID << 8)
                    + (packet->SubID);
            packet->kernel_index = knl; // ָ��Ҫ�����͵�kernel��������0��ʼ
            packet->points = vec;
            packet->resetDelay(Dly_inReqFIFO);
            queue.push_back(packet);
        }
    }
}

/**
 * �����µ�һ��
 */
void startNewLayer()
{
    // ������һ��Ŀ�ʼ
    current_layer++;
    layer_channel = getKernelCount(current_layer-1);
    layer_kernel = getKernelCount(current_layer);
    picker_bandwdith = Picker_FullBandwidth;
    picker_tagret = 0;
    printf("\n========== �����%d�� ==========\n", current_layer);

    // ���ݷָһ���Ӿͷֺ��ˣ�û���ӳ�
    Kernel* kernel = new Kernel(KERNEL_SIDE, getKernelCount(current_layer-1));
    splitMap2Queue(feature_map, kernel, StartQueue);
    delete kernel;
    delete feature_map;
    feature_map = NULL;
}

/**
 * ��һ��queue��������Ƿ������tag
 * ReqQueue�����Data
 * DatLatch�����Request
 * �������Ѿ�û��DatLatch�ˣ��ò�����
 */
bool findTagInQueue(FIFO queue, TagType tag)
{
    for (unsigned int i = 0; i < queue.size(); i++)
    {
        if (queue.at(i)->Tag == tag)
            return true;
    }
    return false;
}

/**
 * �л�����һ��pick��Ŀ��
 */
void pickNextTarget()
{
    picker_tagret++;
    if (picker_tagret >= layer_kernel)
        picker_tagret = 0;
}

/**
 * �ж�ÿ�������˵��������Ƿ�ﵽ�ܹ�����ĳ̶�
 * ������Լ��㣬�� size() >= ƫ��+KERNEL_SIDE * KERNEL_SIDE * layer_channel
 * ����Щ���������ݽ����ۼӣ�������������µ�DataPacket��������һ������
 */
void convCalc()
{

}


// ==================== ���̿��� ===================
// ����ǰ������
void inClock();
void dataOperator();
void dataTransfer();
void clockGoesBy();

/**
 * ��ʼ��һ������Ҫ������
 */
void initFlowControl()
{
    initLayerResource();
    feature_map = new FeatureMap(0, 224, 3);
}

/**
 * ÿ��clock����һ��
 */
void runFlowControl()
{
    while (true)
    {
        Sleep(1); // ����ʾ����

        inClock();

        // ������������һ�㣬���˳�
        if (current_layer > MAX_LAYER)
            break;
    }
}

/**
 * һ��clock�еĲ���
 */
void inClock()
{
    global_clock++;
    picker_bandwdith = Picker_FullBandwidth;

    dataOperator();

    dataTransfer();

    clockGoesBy();
}

/**
 * ���ݲ���
 * �������ݷָ�������ϲ�����һ��clock�������¶������
 */
void dataOperator()
{
    // �����µ�һ�㣬�ָ�����ͼ
    if (feature_map)
    {
        startNewLayer();
    }

    // ÿ�������˽��м���

}

/**
 * ���ݴ���
 * ���has_transfered�������´˷���
 */
void dataTransfer()
{
    // ʹ�ô�flag�����˷Ƕ��̵߳��Ⱥ�˳������
    has_transfered = false;


    // ����ͼ�ĵ㵽ReqFIFO��DatLatch
    // ����ReqFIFO��pick����ָ��ָ��data��data�������ų���
    // �����������ģ�Ӧ�ÿ��Բ��÷ֿ���ֻʹ��һ������
    while (ReqQueue.size() < ReqQueue_MaxSize && !StartQueue.empty())
    {
        DataPacket* packet = StartQueue.front();
        StartQueue.erase(StartQueue.begin()); // ɾ����Ԫ��

        packet->resetDelay(Dly_inReqFIFO);
        DataPacket* req = packet;
        ReqQueue.push_back(req);

        /*DataPacket* data = new DataPacket(packet->data);
        data->Tag = req->Tag;
        DatLatch.push_back(data);*/
        DEB("Start %d => ReqFIFO + DatLatch\n", req->Tag);
    }

    // ReqFIFI => ConvQueues
    int start_picker_target = picker_tagret; // ��¼��ǰ��picker��Ŀ�꣬����ȫ��һ������ѭ��
    while (picker_bandwdith > 0)
    {
        // ������������Ѿ��ﵽ�����ޣ����������kernel
        if (ConvQueue[picker_tagret].size() > ConvQueue_MaxSize)
        {
            pickNextTarget(); // pick����һ��
            if (picker_tagret == start_picker_target) // ȫ����ѯ��һ�鶼���У�ȡ������
                break;
            continue;
        }

        for (int i = 0; i < ReqQueue.size(); i++)
        {
            DataPacket* packet = ReqQueue.at(i);
            // packet �ӳ�û�н��������߸����Ͳ������kernel��
            // ��������picker�������ƺ����ߣ�
            if (!packet->isDelayFinished() || packet->kernel_index != picker_tagret)
                continue;

            // packet�ܷ��ͣ�kernel�ܽ���
            // ��ʼ����
            PickQueue.push_back(packet); // ����Pick�ӳ�
            ReqQueue.erase(ReqQueue.begin() + i--);
            packet->resetDelay(Dly_onPick);
            DEB("���� pick �� Conv %d\n", picker_tagret);

            // ���ͺ������ֵ�ı仯
            picker_bandwdith--;
            pickNextTarget();
        }
    }

    // Pick�ӳٽ���������Conv
    for (int i = 0; i < PickQueue.size(); i++)
    {
        DataPacket* packet = PickQueue.at(i);
        if (!packet->isDelayFinished())
            continue;

        // pick�ӳٽ���������conv
        PickQueue.erase(PickQueue.begin() + i--);
        ConvQueue[packet->kernel_index].push_back(packet);
        packet->resetDelay(Dly_inConv);
        DEB("ReqFIFO pick=> Conv %d\n", picker_tagret);
    }


    /*// ��������ConvPoints�ò����ˣ���Ϊ Conv ��Щ�����Ǵ洢�㣬ֻ�ǵ��delay
    int start_picker_target = picker_tagret; // ��¼��ǰ��picker��Ŀ�꣬����ȫ��һ������ѭ��
    while (picker_bandwdith > 0)
    {
        // ������ݵ�������Ѿ��ﵽ�����ޣ����������kernel
        if (ConvPoints[picker_tagret].size() >= ConvPoints_MaxSize)
        {
            pickNextTarget(); // pick����һ��
            if (picker_tagret == start_picker_target) // ȫ����ѯ��һ�鶼���У�ȡ������
                break;
            continue;
        }

        for (int i = 0; i < ReqQueue.size(); i++)
        {
            DataPacket* packet = ReqQueue.at(i);
            // packet �ӳ�û�н��������߸����Ͳ������kernel��
            // ��������picker�������ƺ����ߣ�
            if (!packet->isDelayFinished() || packet->kernel_index != picker_tagret)
                continue;

            // packet�ܷ��ͣ�kernel�ܽ���
            // ��ʼ����
            PointVec vec = packet->points;
            for (unsigned int p = 0; p < vec.size(); p++)
            {
                ConvPoints[picker_tagret].push_back(vec.at(p));
            }
            DEB("ReqFIFO => Conv %d\n", picker_tagret);

            // ɾ��packet�������Ѿ�����Ҫ���packet�ˣ�
            ReqQueue.erase(ReqQueue.begin() + i--);

            // ���ͺ������ֵ�ı仯
            picker_bandwdith--;
            pickNextTarget();
        }
    }*/


    // DatLatch Pick�� ConvQueue��Ҫ��ȷ����ͬTag��Req�ȷ��Ͳ��ܷ�
    // Data����Req���ͣ������ٵ����ж���
    /*for (unsigned int i = 0; i < DatLatch.size(); i++)
    { }*/


    // ÿ�� ConvQueue ��������͸� SndFIFO����ű���ϵ�ַ
    for (int i = 0; i < layer_kernel; i++)
    {
        FIFO& queue = ConvQueue[i];
        for (unsigned int j = 0; j < queue.size(); j++)
        {
            DataPacket* packet = queue.at(j);
            if (!packet->isDelayFinished())
                continue;

            // Conv => SndFIFO

        }
    }


    // �����������������ݴ���
    // ��ô�������´���
    // ������̻߳����޷�ģ����̵߳Ķ�����ͬ����������
    if (has_transfered)
    {
        dataTransfer(); // �ݹ�����Լ���ֱ�� !has_transfered
    }
}

/**
 * ������һ��clock
 * �����ж����е�packet��delay��next
 */
void clockGoesBy()
{
    for (unsigned int i = 0; i < ReqQueue.size(); i++)
    {
        ReqQueue[i]->delayToNext();
    }

    for (int i = 0; i < layer_kernel; i++)
    {
        for (unsigned int j = 0; j < ConvQueue[i].size(); j++)
        {
            ConvQueue[i].at(j)->delayToNext();
        }
    }
}

/**
 * �������ؽ���
 * ������������ֽ��
 */
void finishFlowControl()
{

}


#endif // FLOWCONTR_H