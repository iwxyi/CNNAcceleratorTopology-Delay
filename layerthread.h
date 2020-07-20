/**
  * 判断当前卷积在哪一层
  * 管理卷积的线程
  */

#ifndef LAYERTHREAD_H
#define LAYERTHREAD_H

#include <stdio.h>
#include <pthread.h>
#include <vector>
#include <algorithm>
#include "convolution.h"

int current_layer = 0;   // 当前正处在第几层
int layer_channel = 3;   // 当前层图片深度，即channel数量
int layer_kernel = 3;    // 当前层的kernel总数量
int finished_kernel = 0; // 当前层结束的kernel数量
std::vector<pthread_t*> conv_thread;   // 子线程对象
std::vector<FeatureMap*> feature_maps; // 每张图

/**
 * 初始化第0层（默认）的数据
 * 以及传入的图片值
 */
void initLayerResource()
{
    current_layer = 0;   // 第0层
    layer_channel = 3;
    layer_kernel = 3;
    finished_kernel = 3; // 第0层的kernel数=第1层的channel=3
    feature_maps.push_back(new FeatureMap(0, MAP_SIDE_MAX, MAP_CHANNEL_DEFULT)); // 默认224*224*3的图
}

/**
 * 获取卷积核的数量
 * 其实每一层数量就是上一层的数量
 * 3*3*3 -> 3*3*8 -> 16 -> 32 -> 32 -> 32...
 */
inline int getKernelCount(int layer)
{
    switch (layer)
    {
    case 0:
        return 3;
    case 1:
        return 8;
    case 2:
        return 16;
    default:
        return 32;
    }
}


/**
 * 单个卷积核进行卷积的线程
 * @return 卷积出来的chnnel张图，合并成1张
 */
void *convolutionThread(void *_arg)
{
    pthread_detach(pthread_self()); // unjoinable，非阻塞，运行结束后退出
    ConvThreadArg* arg = (ConvThreadArg*) _arg;
    FeatureMap* map = arg->map;
    Kernel* kernel = arg->kernel;
    printf("> start convolution thread, kernel: %d\n", arg->k_indx);
    if (map)
        printf("    feature map: %d * %d * %d\n", map->side, map->side, map->channel);
    if (kernel)
        printf("    conv kernel: %d * %d * %d\n", kernel->side, kernel->side, kernel->channel);

    // 开始卷积
    FeatureMap *result = convolution(map, kernel);

    // 结果要传递到下一层
    feature_maps.push_back(result);
    finished_kernel++;

    printf("- conv finished: kernel: %d\n", arg->k_indx);
    // 释放资源
    delete arg;
    delete map;
    pthread_exit(0);
    return 0;
}

/**
 * 释放上一层时new出来的数据
 */
void releasePrevLayerResource()
{
    // 释放上一层指针结果
    while (!feature_maps.empty())
    {
        delete feature_maps.back();
        feature_maps.pop_back();
    }

    // 释放上一层的线程
    while (!conv_thread.empty())
    {
        pthread_join(*conv_thread.back(), NULL);
        conv_thread.pop_back();
    }
}

/**
 * 获取每一层合并后的MAP
* @return 如果正在多线程计算中，导致没有可合并的map，则返回NULL
 */
FeatureMap* getMergedMap()
{
    // 这里确保 finished_kernel == kernel_count == feature_maps.count(), 且 > 0
    if (finished_kernel < layer_kernel) // 多线程未完成，无合并的图，返回NULL
        return NULL;

    // 合并FeatureMap
    FeatureMap* map = NULL;
    if (current_layer <= 0) // 初次使用，不用这么多的操作
    {
        map = feature_maps.front();
        feature_maps.clear(); // map还需要用到，不用delete
        printf("init feature map: %d * %d * %d\n", map->side, map->side, map->channel);
    }
    else
    {
        // 合并上一层每个kernel的FeatureMap
        std::vector<FeatureMap*> prev_map = feature_maps;
        // 按各线程kernel顺序进行排序，有序合并
        std::sort(prev_map.begin(), prev_map.end(), [=](FeatureMap* a, FeatureMap* b){
            return a->kernel < b->kernel;
        });

        int side = prev_map.front()->side;
        map = new FeatureMap(0, side, layer_channel);
        printf("result feature map: %d * %d * %d\n", side, side, layer_channel);
        for (int i = 0; i < layer_channel; i++)
        {
            // memcpy(map->map[i], prev_map.at(i)->map[0], sizeof(INT8)*side*side); // 不是连续内存，无法cpy
            INT8*** p_map = prev_map.at(i)->map;
            for (int y = 0; y < side; y++)
            {
                for (int x = 0; x < side; x++)
                {
                    map->map[y][x][i] = p_map[y][x][0];
                }
            }
        }

        releasePrevLayerResource();
    }
    return map;
}

/**
 * 判断多线程卷积的任务进度
 * @return false时线程未结束；true时表示线程结束
 */
bool judgeConvolutionThreads()
{
    FeatureMap* map = getMergedMap();
    if (!map)
        return false;

    // 最多 MAX_LAYER 层(目前32)
    if (current_layer >= MAX_LAYER)
    {
        // 结果都在上面的的特征图map中。暂时没有输出
        printf("all complete\n");
        return true;
    }

    // 进入下一层
    current_layer++;
    layer_channel = getKernelCount(current_layer-1); // 当前层的channel=上一层的kernel
    layer_kernel = getKernelCount(current_layer);
    printf("\n================ layer:%d ================\n\n", current_layer);
    printf("kernel count = %d\n", layer_kernel);

    // 开启多线程
    finished_kernel = 0; // 已完成的线程数量重置为0
    for (int k = 0; k < layer_kernel; k++)
    {
        // 创建全空数据（图+核）。在线程结束的时候delete掉
        ConvThreadArg* arg = new ConvThreadArg(current_layer, k+1, new FeatureMap(k+1, map), new Kernel(KERNEL_SIDE, layer_channel));

        // 传入多线程。该层子线程全部结束后统一释放
        pthread_t* thread = new pthread_t;
        conv_thread.push_back(thread);
        int ret = pthread_create(thread, NULL, convolutionThread, (void*)arg);
        if (ret != 0)
            printf("pthread_create error: %d\n", ret);
    }
    return false;
}

#endif // LAYERTHREAD_H
