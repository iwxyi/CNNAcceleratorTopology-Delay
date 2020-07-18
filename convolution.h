/**
  * 卷积的相关类以及柯里化方法
  */

#ifndef CONVOLUTION_H
#define CONVOLUTION_H

#include <windows.h>

// ==================== 尺寸定义 ===================
#define MAP_SIDE_MAX 224     // 特征图边长的最大
#define MAP_CHANNEL_DEFULT 3 // 特征图通道默认数量 RGB
#define KERNEL_SIDE 3        // 卷积核固定边长
#define KERNEL_MAX_COUNT 32  // 卷积核最大数量
#define MAX_LAYER 32         // 最多的层数（32还是128）


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
    int k_indx = 0;         // 核的索引，最终与其他核（按顺序）
    FeatureMap *map = NULL; // 图的对象指针
    Kernel *kernel;         // 卷积核的边长
};


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

#endif // CONVOLUTION_H
