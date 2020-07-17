/**
  * 判断当前卷积在哪一层
  * 管理卷积的线程
  */

#ifndef LAYERTHREAD_H
#define LAYERTHREAD_H

#include <stdio.h>
#include <pthread.h>
#include <vector>
#include "convolution.h"

int current_layer = 0;   // 当前正处在第几层
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
    printf("> 开始卷积子线程: kernel: %d\n", arg->k_indx);
    if (map)
        printf("    特征图: %d * %d * %d\n", map->side, map->side, map->channel);
    if (kernel)
        printf("    卷积核: %d * %d * %d\n", kernel->side, kernel->side, kernel->channel);

    // 开始卷积
    FeatureMap *result = convolution(map, kernel);

    // 结果要传递到下一层
    feature_maps.push_back(result);
    finished_kernel++;

    printf("- 卷积结束: kernel: %d\n", arg->k_indx);
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
 * 判断多线程卷积的任务进度
 * @return false时线程未结束；true时表示线程结束
 */
bool judgeConvolutionThreads()
{
    int kernel_count = getKernelCount(current_layer); // 上一层的kernel数量
    // 这里确保 finished_kernel == kernel_count == feature_maps.count(), 且 > 0
    if (finished_kernel < kernel_count)
        return false;

    // 合并FeatureMap
    FeatureMap* map = NULL;
    int channel_count = kernel_count; // 这一层的channel数量 = 上一层的kernel数量 = 上一层生成map的数量
    if (current_layer <= 0) // 初次使用，不用这么多的操作
    {
        map = feature_maps.front();
        feature_maps.clear(); // map还需要用到，不用delete
        printf("初始特征图：%d * %d * %d\n", map->side, map->side, map->channel);
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
        map = new FeatureMap(0, side, channel_count);
        printf("合并特征图：%d * %d * %d\n", side, side, channel_count);
        for (int i = 0; i < channel_count; i++)
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

    // 最多 MAX_LAYER 层(目前32)
    if (current_layer >= MAX_LAYER)
    {
        // 结果都在上面的的特征图map中。暂时没有输出
        printf("全部运行结束");
        return true;
    }

    // 进入下一层
    current_layer++;
    printf("\n================ 进入第%d层 ================\n\n", current_layer);
    kernel_count = getKernelCount(current_layer); // 当前层的kernel数量
    printf("kernel count = %d\n", kernel_count);

    // 开启多线程
    finished_kernel = 0; // 已完成的线程数量重置为0
    for (int k = 0; k < kernel_count; k++)
    {
        // 创建全空数据（图+核）。在线程结束的时候delete掉
        ConvThreadArg* arg = new ConvThreadArg(current_layer, k+1, new FeatureMap(k+1, map), new Kernel(KERNEL_SIDE, channel_count));

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
