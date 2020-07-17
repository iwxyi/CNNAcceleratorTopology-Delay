#include <stdio.h>
#include <pthread.h>
#include <algorithm>
#include <queue>
#include <windows.h>

#define MAP_SIDE_MAX 224    // 图边长的最大
#define MAP_CHANNEL_DEFULT 3       // 图通道默认数量 RGB
#define KERNEL_SIDE 3       // 卷积核固定边长
#define KERNEL_MAX_COUNT 32 // 卷积核最大数量
#define MAX_LAYER 32        // 最多的层数（32还是128）

struct FeatureMap;

//int layer_count[KERNEL_MAX_COUNT]; // 每一层正在卷积的数量。允许多层同时运行才能用到
//int thread_finished[KERNEL_MAX_COUNT]; // 每个卷积线程是否计算结束，0=未运行, 1=运行中, -1=已结束
int current_layer = 0;   // 当前正处在第几层
int finished_kernel = 0; // 当前层结束的kernel数量
std::vector<FeatureMap*> feature_maps; // 每张图

INT8*** create3D(int y, int x, int z);

/**
 * 图类
 * 包含了标号和图的层数
 */
struct FeatureMap {
    FeatureMap(){}
    FeatureMap(int k, int side, int channel) : FeatureMap(side, channel)
    {
        this->kernel = k;
    }
    FeatureMap(int side, int channel) : side(side), channel(channel)
    {
        initMap();
    }
    FeatureMap(int k, FeatureMap *map)
    {
        this->kernel = k;
        this->side = map->side;
        this->channel = map->channel;
        initMap();
//        printf("initMap finished: %d, %d, %d  %d~%d\n", side, side, channel, this->map[side-1][side-1][channel-1], map->map[side-1][side-1][channel-1]);
//        memcpy(this->map, map->map, sizeof(INT8)*side*side*channel); // 莫名的崩溃
//        printf("memcpy finished\n");
        for (int y = 0; y < side; y++)
            for (int x = 0; x < side; x++)
                for (int z = 0; z < channel; z++)
                    this->map[y][x][z] = map->map[y][x][z];
    }
    ~FeatureMap()
    {
        if (map)
        {
            for (int y = 0; y < side; y++)
            {
                for (int x = 0; x < side; x++)
                {
                    delete[] map[y][x];
                }
                delete map[y];
            }
            delete[] map;
        }
    }

    int kernel = 0;     // kernel 标号。被滑的图不需要这项
    int side = 0;       // 图的边长（正方形）
    int channel = 0;    // 图的channel数量
    INT8 ***map = NULL; // 图：为了遍历方便，为：map[channel][side][side]

    /**
     * 初始化图，全部都默认0
     * @param m 传进来的图，如果为NULL则全部设置成0
     */
    void initMap(INT8***m = NULL)
    {
        map = m ? m : create3D(side, side, channel);
    }
};

/**
 * 卷积核类
 */
struct Kernel {
    Kernel(): side(3), channel(3) {}
    Kernel(int side, int channel)
        : side(side), channel(channel)
    {
        initKernel();
    }
    int side;    // 边长 side * side
    int channel; // 等于当前被滑动的图的channel数量
    INT8 ***bits = NULL; // 每一位的值

    /**
     * 初始化kernel
     * @param k 如果为NULL，则全部为0
     */
    void initKernel(INT8*** k = NULL)
    {
        bits = k ? k : create3D(side, side, channel);
    }
};

/**
 * 线程传递参数类
 * kernel.channel == image.channel
 * kernel.filter = 下一层 image.channel
 */
struct ConvThreadArg {
    ConvThreadArg(){}
    ConvThreadArg(int layer, int k, FeatureMap *img, Kernel *kernel)
        : layer(layer), k_indx(k), map(img), kernel(kernel)
    {}
    int layer = 0;          // 当前是第几层
    int k_indx = 0;         // 核的索引，传给下一个核
    FeatureMap *map = NULL; // 图的对象指针
    Kernel *kernel;         // 卷积核的边长
};

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
 * 进行卷积的计算函数
 */
FeatureMap* convolution(FeatureMap *image, Kernel *kernel)
{
    int new_side = image->side - kernel->side + 1;
    FeatureMap* result = new FeatureMap(image->kernel, new_side, 1);
    INT8*** map = result->map;

    // 累加（注意：这里坐标反着的，先是y，再是x）
    for (int y = 0; y < new_side; y++)
    {
        for (int x = 0; x < new_side; x++)
        {
            // 新图的位置：map[y][x][ch]
            // TODO：这里可以加个缓存来加快速度
            INT8& v = map[y][x][0];
            for (int i = 0; i < kernel->side; i++)
                for (int j = 0; j < kernel->side; j++)
                    for (int k = 0; k < kernel->channel; k++)
                        v += image->map[i][j][k];
        }
    }
    return result;
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

int main()
{
    // 开多线程
    std::vector<pthread_t*> conv_thread;

    // 依次传入通道
    current_layer = 0;   // 第0层
    finished_kernel = 3; // 第0层的kernel数=第1层的channel=3
    feature_maps.push_back(new FeatureMap(0, MAP_SIDE_MAX, MAP_CHANNEL_DEFULT)); // 默认224*224*3的图

    // 死循环一直等到picker
    while (true)
    {
        Sleep(1); // 避免直接卡死
        int kernel_count = getKernelCount(current_layer); // 上一层的kernel数量
        // 这里确保 finished_kernel == kernel_count == feature_maps.count(), 且 > 0
        if (finished_kernel < kernel_count)
            continue;

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

        // 最多 MAX_LAYER 层(目前32)
        if (current_layer >= MAX_LAYER)
        {
            // 结果都在上面的的特征图map中。暂时没有输出
            printf("全部运行结束");
            break;
        }
        // 进入下一层
        current_layer++;
        printf("\n================ 进入第%d层 ================\n\n", current_layer);
        kernel_count = getKernelCount(current_layer); // 当前层的kernel数量
        printf("kernel count = %d\n", kernel_count);
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
    }

    pthread_exit(NULL);
    return 0;
}

/**
 * 创建固定大小的三维数组
 * 记得手动delete[]，免得内存泄漏
 * 如果要优化速度，可以把Z放在最外层循环
 */
INT8*** create3D(int y, int x, int z)
{
    INT8*** bits = new INT8**[y];
    for (int i = 0; i < y; i++)
    {
        bits[i] = new INT8*[x];
        for (int j = 0; j < x; j++)
        {
            bits[i][j] = new INT8[z];
            for (int k = 0; k < z; k++)
                bits[i][j][k] = 0;
        }
    }
    return bits;
}
