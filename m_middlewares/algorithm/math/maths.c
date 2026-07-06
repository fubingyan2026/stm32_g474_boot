/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <string.h>

#include "maths.h"

// ============================================================================
// 快速三角函数逼近（FAST_MATH / VERY_FAST_MATH）
// 使用 minimax 多项式替代标准 libc 三角函数，牺牲微小精度换取速度。
// VERY_FAST_MATH 使用 7 阶多项式，FAST_MATH 使用 9 阶多项式。
// ============================================================================
#if defined(FAST_MATH) || defined(VERY_FAST_MATH)
#if defined(VERY_FAST_MATH)

// VERY_FAST_MATH: 7 阶 Chebyshev 多项式系数
// sin_approx 最大绝对误差 = 2.305023e-06
// cos_approx 最大绝对误差 = 2.857298e-06
// 参考:
// http://lolengine.net/blog/2011/12/21/better-function-approximations
// https://github.com/cleanflight/cleanflight/issues/940#issuecomment-110323384
#define sinPolyCoef3 (-1.666568107e-1f)
#define sinPolyCoef5 8.312366210e-3f
#define sinPolyCoef7 (-1.849218155e-4f)
#define sinPolyCoef9 0
#else
// FAST_MATH: 9 阶多项式系数（更高精度）
#define sinPolyCoef3 -1.666665710e-1f // Double: -1.666665709650470145824129400050267289858e-1
#define sinPolyCoef5 8.333017292e-3f // Double:  8.333017291562218127986291618761571373087e-3
#define sinPolyCoef7 -1.980661520e-4f // Double: -1.980661520135080504411629636078917643846e-4
#define sinPolyCoef9 2.600054768e-6f // Double:  2.600054767890361277123254766503271638682e-6
#endif

/**
 * @brief 使用 Quake III 算法快速计算浮点数的倒数平方根
 *
 * 经典的 fast inverse square root 实现，使用魔数 0x5f375a86 做初始猜测，
 * 再通过两轮牛顿迭代优化精度。比 1.0f/sqrtf(x) 快约 3~4 倍。
 *
 * 注意：使用 memcpy 而非指针强制转换，以避免 Strict Aliasing 未定义行为。
 *
 * @param num 输入值
 * @return    1 / sqrt(num) 的近似值
 */
float inv_sqrt_approx(const float num)
{
    const float halfnum = 0.5f * num;
    float y = num;
    uint32_t i;
    memcpy(&i, &y, sizeof(i));
    i = 0x5f375a86 - (i >> 1); // 使用优化的常数（原版 0x5f3759df 的改进）
    memcpy(&y, &i, sizeof(y));

    // 第一轮牛顿迭代
    y = y * (1.5f - halfnum * y * y);
    // 第二轮牛顿迭代（提高精度）
    y = y * (1.5f - halfnum * y * y);
    return y;
}

/**
 * @brief 快速计算浮点数的平方根
 *
 * 利用 inv_sqrt_approx 实现：sqrt(x) = x * (1/sqrt(x))。
 * 精度约 2e-5 量级，适合电机控制等对速度要求高于精度的场景。
 *
 * @param num 输入值
 * @return    sqrt(num) 的近似值
 */
float sqrt_approx(const float num)
{
    return num * inv_sqrt_approx(num);
}

/**
 * @brief 使用多项式逼近计算 sin(x)
 *
 * 1. 先将角度归一化到 [-PI, PI]
 * 2. 利用对称性将范围缩小到 [-PI/2, PI/2]
 * 3. 使用奇次多项式 sin(x) ≈ x + x*x2*(c3 + x2*(c5 + x2*(c7 + x2*c9)))
 *
 * @param x 输入角度（弧度）
 * @return  sin(x) 的近似值
 */
float sin_approx(float x)
{
    utils_norm_angle_rad(&x);
    if (x > 0.5f * M_PIf)
        x = 0.5f * M_PIf - (x - 0.5f * M_PIf); // 折叠到 [-90°, +90°]
    else if (x < -(0.5f * M_PIf))
        x = -(0.5f * M_PIf) - ((0.5f * M_PIf) + x);
    const float x2 = x * x;
    return x + x * x2 * (sinPolyCoef3 + x2 * (sinPolyCoef5 + x2 * (sinPolyCoef7 + x2 * sinPolyCoef9)));
}

/**
 * @brief 计算 cos(x) 的近似值
 *
 * 利用三角恒等式 cos(x) = sin(x + π/2) 实现，精度与 sin_approx 一致。
 *
 * @param x 输入角度（弧度）
 * @return  cos(x) 的近似值
 */
float cos_approx(const float x)
{
    return sin_approx(x + 0.5f * M_PIf);
}

/**
 * @brief 使用有理多项式逼近计算 atan2(y, x)
 *
 * 基于有理分式逼近：将 y/x（取 min/max 比值）映射到 [0, 1]，
 * 然后通过 7 系数有理函数逼近 arctan，最后根据象限恢复正确角度。
 *
 * 最大绝对误差 = 7.152557e-07 rad（约 4.1e-05 度）
 *
 * 参考：
 * - 初始实现: Crashpilot1000 (HarakiriWebstore)
 * - 多项式系数: Andor (dsprelated.com)，经 Ledvinap 优化减少一次乘法
 *
 * @param y 对边长度
 * @param x 邻边长度
 * @return  atan2(y, x) 的近似值（弧度），范围 [-PI, PI]
 */
float atan2_approx(const float y, const float x)
{
#define atanPolyCoef1 3.14551665884836e-07f
#define atanPolyCoef2 0.99997356613987f
#define atanPolyCoef3 0.14744007058297684f
#define atanPolyCoef4 0.3099814292351353f
#define atanPolyCoef5 0.05030176425872175f
#define atanPolyCoef6 0.1471039133652469f
#define atanPolyCoef7 0.6444640676891548f

    float res;
    float absX = ABS_M(x);
    float absY = ABS_M(y);
    res = MAX_M(absX, absY);
    if (res)
        res = MIN_M(absX, absY) / res; // 将比值映射到 [0, 1]
    else
        res = 0.0f;
    // 有理多项式逼近 arctan(res)
    res = -((((atanPolyCoef5 * res - atanPolyCoef4) * res - atanPolyCoef3) * res - atanPolyCoef2) * res - atanPolyCoef1) / ((atanPolyCoef7 * res + atanPolyCoef6) * res + 1.0f);
    // 根据象限恢复正确角度
    if (absY > absX)
        res = (M_PIf * 0.5f) - res;
    if (x < 0)
        res = M_PIf - res;
    if (y < 0)
        res = -res;
    return res;
}

/**
 * @brief 使用多项式逼近计算 acos(x)
 *
 * 基于 NVIDIA Cg 手册的 arccos 实现：
 * acos(x) ≈ sqrt(1-|x|) * P(|x|)，其中 P 为 3 阶多项式。
 *
 * 最大绝对误差 = 6.760856e-05 rad（约 3.87e-03 度）
 *
 * 参考：
 * - http://developer.nvidia.com/Cg/acos.html
 * - M. Abramowitz and I.A. Stegun, Handbook of Mathematical Functions
 *
 * @param x 输入值，应在 [-1, 1] 范围内
 * @return  acos(x) 的近似值（弧度），范围 [0, PI]
 */
float acos_approx(const float x)
{
    const float xa = fabsf(x);
    const float result = sqrt_approx(1.0f - xa) * (1.5707288f + xa * (-0.2121144f + xa * (0.0742610f + (-0.0187293f * xa))));
    if (x < 0.0f)
        return M_PIf - result;
    return result;
}

/**
 * @brief 计算 asin(x) 的近似值
 *
 * 利用恒等式 asin(x) = π/2 - acos(x) 实现，精度与 acos_approx 一致。
 *
 * @param x 输入值，应在 [-1, 1] 范围内
 * @return  asin(x) 的近似值（弧度），范围 [-PI/2, PI/2]
 */
float asin_approx(const float x)
{
    return M_PIf * 0.5f - acos_approx(x);
}

/**
 * @brief 计算 tan(x) 的近似值
 *
 * 基于 sin_approx / cos_approx 实现。当 cos(x) 接近 0 时（|cos(x)| < 1e-6），
 * 返回 ±1e6 代替 ±Inf，避免浮点异常传播。
 *
 * @param x 输入角度（弧度）
 * @return  tan(x) 的近似值
 */
float tan_approx(const float x)
{
    const float c = cos_approx(x);
    if (c > -1e-6f && c < 1e-6f) {
        return (sin_approx(x) >= 0.0f) ? 1e6f : -1e6f;
    }
    return sin_approx(x) / c;
}

/**
 * @brief 使用范围归约 + 多项式逼近计算 e^x
 *
 * 算法：exp(x) = 2^(x / ln(2))
 * 1. 令 t = x / ln(2)，分解为整数 k + 小数 r（r ∈ [-0.5, 0.5]）
 * 2. 用 4 阶 minimax 多项式逼近 2^r
 * 3. 结果 = 2^r * 2^k（2^k 通过 IEEE 754 位操作直接构造）
 *
 * 已处理：NaN 传播、输入过大/过小时的指数溢出/下溢、int32_t 转换 UB。
 *
 * @param x 指数值
 * @return  e^x 的近似值
 */
float exp_approx(float x)
{
    // 处理 NaN
    if (x != x)
        return x;

    const float t = x * 1.442695041f; // 1 / ln(2)

    // 限制范围以防止 (int32_t) 强制转换溢出（UB）
    // k ∈ [-127, 128] 可覆盖单精度浮点数的全部指数范围
    float clamped_t = t;
    if (clamped_t > 128.0f)
        clamped_t = 128.0f;
    else if (clamped_t < -127.0f)
        clamped_t = -127.0f;

    int32_t k;
    if (clamped_t >= 0.0f) {
        k = (int32_t)(clamped_t + 0.5f);
    } else {
        k = (int32_t)(clamped_t - 0.5f);
    }

    // 必须使用 clamped_t 计算 r，防止 x 极大时 r 超出 [-0.5, 0.5]
    const float r = clamped_t - (float)k;

    // 2^r 在 [-0.5, 0.5] 上的 4 阶 minimax 多项式
    const float poly = r * (0.69314718f + r * (0.24022651f + r * (0.05550411f + r * 0.00961813f)));
    const float two_r = 1.0f + poly;

    // 构造 2^k
    // k = -127 → 指数位为 0，即 two_k = 0.0f（自然下溢）
    // k = 128  → 指数位为 255，即 two_k = INF   （自然上溢）
    uint32_t bits = (uint32_t)(k + 127) << 23;
    float two_k;
    memcpy(&two_k, &bits, sizeof(two_k));

    return two_r * two_k;
}

/**
 * @brief 使用区间归约 + 多项式逼近计算 ln(x)
 *
 * 算法：
 * 1. 从 IEEE 754 浮点数中提取指数 e 和尾数 m（m ∈ [1, 2)）
 * 2. 将 m 归约到 [1/√2, √2) ≈ [0.707, 1.414)，令 t = m - 1
 * 3. 用 4 阶 minimax 多项式逼近 ln(1+t)
 * 4. 结果 = e * ln(2) + ln(1+t)
 *
 * 已处理：NaN/Inf 传播、x ≤ 0 返回 NaN 或 -Inf。
 *
 * @param x 输入值，必须 > 0
 * @return  ln(x) 的近似值
 */
float log_approx(float x)
{
    if (x != x)
        return x; // NaN 直接返回
    if (x <= 0.0f) {
        return (x == 0.0f) ? -INFINITY : NAN;
    }

    uint32_t bits;
    memcpy(&bits, &x, sizeof(bits));

    // 正无穷直接返回
    if ((bits & 0x7F800000) == 0x7F800000) {
        return x;
    }

    int32_t e = (int32_t)((bits >> 23) & 0xFF) - 127;
    bits = (bits & 0x007FFFFF) | 0x3F800000; // 将尾数归一化到 [1, 2)
    float m;
    memcpy(&m, &bits, sizeof(m));

    // 区间归约：从 [1, 2) 收缩到 [1/√2, √2)，使 t 的范围从 [0, 1) 缩小为 [-0.2929, 0.4142]
    // 大幅提升多项式逼近精度
    if (m > 1.41421356f) {
        m *= 0.5f;
        e += 1;
    }
    const float t = m - 1.0f;

    // ln(1+t) 在 [-0.2929, 0.4142] 上的 4 阶 minimax 多项式
    const float log_m = t * (0.99999642f + t * (-0.49987412f + t * (0.33179902f + t * (-0.24073380f))));

    return (float)e * 0.69314718f + log_m;
}

/**
 * @brief 计算 a^b 的近似值，通过 pow(a,b) = exp(b * ln(a)) 组合实现
 *
 * 特殊情况处理：
 * - a > 0：直接使用 exp(b * ln(a))
 * - a = 0：b>0 返回 0，b=0 返回 1，b<0 返回 +Inf
 * - a < 0：仅当 b 为精确整数时计算，奇数次幂保留负号；否则返回 NaN
 *
 * @param a 底数
 * @param b 指数
 * @return  a^b 的近似值
 */
float pow_approx(float a, float b)
{
    if (a < 0.0f) {
        // 安全判断 b 是否为精确整数
        float b_floor = floorf(b);
        if (b_floor == b) {
            const float result = exp_approx(b * log_approx(-a));
            // 当 |b| > 2^24 时，单精度浮点数无法表示奇数（所有整数均为偶数）
            int is_odd = 0;
            if (fabsf(b) < 16777216.0f) {
                is_odd = ((int32_t)b) & 1;
            }
            return is_odd ? -result : result;
        }
        return NAN; // 负数的非整数次幂结果为 NaN
    }
    if (a == 0.0f) {
        if (b > 0.0f)
            return 0.0f;
        if (b == 0.0f)
            return 1.0f;
        return INFINITY; // 0 的负数次幂为正无穷
    }
    return exp_approx(b * log_approx(a));
}

#endif // defined(FAST_MATH) || defined(VERY_FAST_MATH)

/**
 * @brief 使用欧几里得算法计算两个整数的最大公约数（GCD）
 *
 * 递归实现，基于 GCD(a, b) = GCD(b, a mod b)，直到余数为 0。
 *
 * @param num   第一个整数
 * @param denom 第二个整数
 * @return      最大公约数
 */
int gcd(const int num, const int denom)
{
    if (denom == 0) {
        return num;
    }

    return gcd(denom, num % denom);
}

/**
 * @brief 计算浮点数的整数次幂
 *
 * 通过循环相乘实现，仅支持非负整数指数。对于大指数效率较低，建议用快速幂算法。
 *
 * @param base 底数
 * @param exp  指数（必须为非负整数）
 * @return     base^exp
 */
float powerf(const float base, const int exp)
{
    float result = base;
    for (int count = 1; count < exp; count++)
        result *= base;

    return result;
}

/**
 * @brief 对 int32_t 值施加死区
 *
 * 若 |value| < deadband 则返回 0，否则从 value 中减去（或加上）死区量，
 * 使得输出在死区边缘连续无跳变。
 *
 * @param value    输入值
 * @param deadband 死区阈值
 * @return         施加死区后的值
 */
int32_t applyDeadband(const int32_t value, const int32_t deadband)
{
    if (ABS_M(value) < deadband) {
        return 0;
    }

    return value >= 0 ? value - deadband : value + deadband;
}

/**
 * @brief 对 float 值施加死区（浮点版本）
 *
 * @param value    输入值
 * @param deadband 死区阈值
 * @return         施加死区后的值
 */
float fapplyDeadband(const float value, const float deadband)
{
    if (fabsf(value) < deadband) {
        return 0;
    }

    return value >= 0 ? value - deadband : value + deadband;
}

/**
 * @brief 重置标准差计算器的内部状态
 *
 * 清零样本计数，准备开始新一轮 Welford 在线统计算法。
 *
 * @param dev 标准差计算器
 */
void devClear(stdev_t* dev)
{
    dev->m_n = 0;
}

/**
 * @brief 向标准差计算器中推入一个新样本
 *
 * 使用 Welford 在线算法增量更新均值和平方差之和，
 * 无需存储全部历史样本，单次更新 O(1) 时间复杂度。
 *
 * @param dev 标准差计算器
 * @param x   新样本值
 */
void devPush(stdev_t* dev, const float x)
{
    dev->m_n++;
    if (dev->m_n == 1) {
        dev->m_oldM = dev->m_newM = x;
        dev->m_oldS = 0.0f;
    } else {
        dev->m_newM = dev->m_oldM + (x - dev->m_oldM) / dev->m_n;
        dev->m_newS = dev->m_oldS + (x - dev->m_oldM) * (x - dev->m_newM);
        dev->m_oldM = dev->m_newM;
        dev->m_oldS = dev->m_newS;
    }
}

/**
 * @brief 获取样本方差（无偏估计）
 *
 * 使用 Bessel 修正（除以 n-1 而非 n），仅在 n > 1 时有效。
 *
 * @param dev 标准差计算器
 * @return    样本方差
 */
float devVariance(const stdev_t* dev)
{
    return dev->m_n > 1 ? dev->m_newS / (dev->m_n - 1) : 0.0f;
}

/**
 * @brief 获取样本标准差
 *
 * 对方差取平方根。注意：此处使用标准库 sqrtf 而非 sqrt_approx，
 * 因为统计计算对精度要求较高。
 *
 * @param dev 标准差计算器
 * @return    样本标准差
 */
float devStandardDeviation(const stdev_t* dev)
{
    return sqrt_approx(devVariance(dev));
}

/**
 * @brief 将度转换为弧度
 *
 * @param degrees 角度值（度）
 * @return        角度值（弧度）
 */
float degreesToRadians(const int16_t degrees)
{
    return degrees * RAD;
}

/**
 * @brief 将整数 x 从源范围线性映射到目标范围
 *
 * 使用 int64_t 中间变量避免乘法溢出。
 *
 * @param x        待映射的值
 * @param srcFrom  源范围下限
 * @param srcTo    源范围上限
 * @param destFrom 目标范围下限
 * @param destTo   目标范围上限
 * @return         映射后的值
 */
int scaleRange(const int x, const int srcFrom, const int srcTo, const int destFrom, const int destTo)
{
    const long int a = ((long int)destTo - (long int)destFrom) * ((long int)x - (long int)srcFrom);
    const long int b = (long int)srcTo - (long int)srcFrom;
    return a / b + destFrom;
}

/**
 * @brief 将浮点数 x 从源范围线性映射到目标范围（浮点版本）
 *
 * @param x        待映射的值
 * @param srcFrom  源范围下限
 * @param srcTo    源范围上限
 * @param destFrom 目标范围下限
 * @param destTo   目标范围上限
 * @return         映射后的值
 */
float scaleRangef(float x, float srcFrom, float srcTo, float destFrom, float destTo)
{
    float a = (destTo - destFrom) * (x - srcFrom);
    float b = srcTo - srcFrom;
    return (a / b) + destFrom;
}

/**
 * @brief 将 3D 向量归一化为单位向量
 *
 * 计算向量长度后每个分量除以长度。若长度为零则不修改 dest。
 *
 * @param src  输入向量
 * @param dest 输出单位向量（可与 src 相同）
 */
void normalizeV(const struct fp_vector* src, struct fp_vector* dest)
{
    const float length = sqrt_approx(src->X * src->X + src->Y * src->Y + src->Z * src->Z);
    if (length != 0) {
        dest->X = src->X / length;
        dest->Y = src->Y / length;
        dest->Z = src->Z / length;
    }
}

/**
 * @brief 根据欧拉角构建 3x3 旋转矩阵
 *
 * 使用 ZYX 顺序（先偏航 yaw，再俯仰 pitch，再横滚 roll）构建旋转矩阵。
 * 三角函数使用快速逼近版本（cos_approx / sin_approx）。
 *
 * 矩阵元素定义：
 * | cosZ*cosY              -cosY*sinZ               sinY          |
 * | sinZ*cosX+cosZ*sinX*sinY  cosZ*cosX-sinZ*sinX*sinY  -sinX*cosY |
 * | sinZ*sinX-cosZ*cosX*sinY  cosZ*sinX+sinZ*cosX*sinY   cosY*cosX |
 *
 * @param delta  包含 roll/pitch/yaw 欧拉角的结构体（弧度）
 * @param matrix 输出的 3x3 旋转矩阵
 */
void buildRotationMatrix(const fp_angles_t* delta, float matrix[3][3])
{
    const float cosx = cos_approx(delta->angles.roll);
    const float sinx = sin_approx(delta->angles.roll);
    const float cosy = cos_approx(delta->angles.pitch);
    const float siny = sin_approx(delta->angles.pitch);
    const float cosz = cos_approx(delta->angles.yaw);
    const float sinz = sin_approx(delta->angles.yaw);

    const float coszcosx = cosz * cosx;
    const float sinzcosx = sinz * cosx;
    const float coszsinx = sinx * cosz;
    const float sinzsinx = sinx * sinz;

    matrix[0][X] = cosz * cosy;
    matrix[0][Y] = -cosy * sinz;
    matrix[0][Z] = siny;
    matrix[1][X] = sinzcosx + coszsinx * siny;
    matrix[1][Y] = coszcosx - sinzsinx * siny;
    matrix[1][Z] = -sinx * cosy;
    matrix[2][X] = sinzsinx - coszcosx * siny;
    matrix[2][Y] = coszsinx + sinzcosx * siny;
    matrix[2][Z] = cosy * cosx;
}

/**
 * @brief 使用欧拉角旋转 3D 向量
 *
 * 先根据 delta 中的 roll/pitch/yaw 构建旋转矩阵，再用该矩阵对向量 v 做旋转变换。
 * 向量被原地修改。
 *
 * @param v     待旋转的向量（原地修改）
 * @param delta 欧拉角（弧度）
 */
void rotateV(struct fp_vector* v, const fp_angles_t* delta)
{
    const struct fp_vector v_tmp = *v;

    float matrix[3][3];

    buildRotationMatrix(delta, matrix);

    v->X = v_tmp.X * matrix[0][X] + v_tmp.Y * matrix[1][X] + v_tmp.Z * matrix[2][X];
    v->Y = v_tmp.X * matrix[0][Y] + v_tmp.Y * matrix[1][Y] + v_tmp.Z * matrix[2][Y];
    v->Z = v_tmp.X * matrix[0][Z] + v_tmp.Y * matrix[1][Z] + v_tmp.Z * matrix[2][Z];
}

// ============================================================================
// 快速中值滤波器（Quick Median Filter）
// N. Devillard 的排序网络实现
// 参考: http://ndevilla.free.fr/median/median.pdf
// 特点：固定窗口大小，使用最优比较网络，无需完整排序即可找到中位数
// ============================================================================

// 整数版排序网络宏
#define QMF_SORT(a, b)          \
    {                           \
        if ((a) > (b))          \
            QMF_SWAP((a), (b)); \
    }
#define QMF_SWAP(a, b)      \
    {                       \
        int32_t temp = (a); \
        (a) = (b);          \
        (b) = temp;         \
    }
#define QMF_COPY(p, v, n)       \
    {                           \
        int32_t i;              \
        for (i = 0; i < n; i++) \
            p[i] = v[i];        \
    }

// 浮点版排序网络宏
#define QMF_SORTF(a, b)          \
    {                            \
        if ((a) > (b))           \
            QMF_SWAPF((a), (b)); \
    }
#define QMF_SWAPF(a, b)   \
    {                     \
        float temp = (a); \
        (a) = (b);        \
        (b) = temp;       \
    }

/**
 * @brief 3 样本快速中值滤波（int32_t）
 *
 * 使用排序网络找 3 个值的中位数，仅需 3 次比较/交换。
 *
 * @param v 包含 3 个 int32_t 的数组
 * @return  中位数
 */
int32_t quickMedianFilter3(int32_t* v)
{
    int32_t p[3];
    QMF_COPY(p, v, 3);

    QMF_SORT(p[0], p[1]);
    QMF_SORT(p[1], p[2]);
    QMF_SORT(p[0], p[1]);
    return p[1];
}

/**
 * @brief 5 样本快速中值滤波（int32_t）
 * @param v 包含 5 个 int32_t 的数组
 * @return  中位数
 */
int32_t quickMedianFilter5(int32_t* v)
{
    int32_t p[5];
    QMF_COPY(p, v, 5);

    QMF_SORT(p[0], p[1]);
    QMF_SORT(p[3], p[4]);
    QMF_SORT(p[0], p[3]);
    QMF_SORT(p[1], p[4]);
    QMF_SORT(p[1], p[2]);
    QMF_SORT(p[2], p[3]);
    QMF_SORT(p[1], p[2]);
    return p[2];
}

/**
 * @brief 7 样本快速中值滤波（int32_t）
 * @param v 包含 7 个 int32_t 的数组
 * @return  中位数
 */
int32_t quickMedianFilter7(int32_t* v)
{
    int32_t p[7];
    QMF_COPY(p, v, 7);

    QMF_SORT(p[0], p[5]);
    QMF_SORT(p[0], p[3]);
    QMF_SORT(p[1], p[6]);
    QMF_SORT(p[2], p[4]);
    QMF_SORT(p[0], p[1]);
    QMF_SORT(p[3], p[5]);
    QMF_SORT(p[2], p[6]);
    QMF_SORT(p[2], p[3]);
    QMF_SORT(p[3], p[6]);
    QMF_SORT(p[4], p[5]);
    QMF_SORT(p[1], p[4]);
    QMF_SORT(p[1], p[3]);
    QMF_SORT(p[3], p[4]);
    return p[3];
}

/**
 * @brief 9 样本快速中值滤波（int32_t）
 * @param v 包含 9 个 int32_t 的数组
 * @return  中位数
 */
int32_t quickMedianFilter9(int32_t* v)
{
    int32_t p[9];
    QMF_COPY(p, v, 9);

    QMF_SORT(p[1], p[2]);
    QMF_SORT(p[4], p[5]);
    QMF_SORT(p[7], p[8]);
    QMF_SORT(p[0], p[1]);
    QMF_SORT(p[3], p[4]);
    QMF_SORT(p[6], p[7]);
    QMF_SORT(p[1], p[2]);
    QMF_SORT(p[4], p[5]);
    QMF_SORT(p[7], p[8]);
    QMF_SORT(p[0], p[3]);
    QMF_SORT(p[5], p[8]);
    QMF_SORT(p[4], p[7]);
    QMF_SORT(p[3], p[6]);
    QMF_SORT(p[1], p[4]);
    QMF_SORT(p[2], p[5]);
    QMF_SORT(p[4], p[7]);
    QMF_SORT(p[4], p[2]);
    QMF_SORT(p[6], p[4]);
    QMF_SORT(p[4], p[2]);
    return p[4];
}

/**
 * @brief 3 样本快速中值滤波（float）
 * @param v 包含 3 个 float 的数组
 * @return  中位数
 */
float quickMedianFilter3f(float* v)
{
    float p[3];
    QMF_COPY(p, v, 3);

    QMF_SORTF(p[0], p[1]);
    QMF_SORTF(p[1], p[2]);
    QMF_SORTF(p[0], p[1]);
    return p[1];
}

/**
 * @brief 5 样本快速中值滤波（float）
 * @param v 包含 5 个 float 的数组
 * @return  中位数
 */
float quickMedianFilter5f(float* v)
{
    float p[5];
    QMF_COPY(p, v, 5);

    QMF_SORTF(p[0], p[1]);
    QMF_SORTF(p[3], p[4]);
    QMF_SORTF(p[0], p[3]);
    QMF_SORTF(p[1], p[4]);
    QMF_SORTF(p[1], p[2]);
    QMF_SORTF(p[2], p[3]);
    QMF_SORTF(p[1], p[2]);
    return p[2];
}

/**
 * @brief 7 样本快速中值滤波（float）
 * @param v 包含 7 个 float 的数组
 * @return  中位数
 */
float quickMedianFilter7f(float* v)
{
    float p[7];
    QMF_COPY(p, v, 7);

    QMF_SORTF(p[0], p[5]);
    QMF_SORTF(p[0], p[3]);
    QMF_SORTF(p[1], p[6]);
    QMF_SORTF(p[2], p[4]);
    QMF_SORTF(p[0], p[1]);
    QMF_SORTF(p[3], p[5]);
    QMF_SORTF(p[2], p[6]);
    QMF_SORTF(p[2], p[3]);
    QMF_SORTF(p[3], p[6]);
    QMF_SORTF(p[4], p[5]);
    QMF_SORTF(p[1], p[4]);
    QMF_SORTF(p[1], p[3]);
    QMF_SORTF(p[3], p[4]);
    return p[3];
}

/**
 * @brief 9 样本快速中值滤波（float）
 * @param v 包含 9 个 float 的数组
 * @return  中位数
 */
float quickMedianFilter9f(float* v)
{
    float p[9];
    QMF_COPY(p, v, 9);

    QMF_SORTF(p[1], p[2]);
    QMF_SORTF(p[4], p[5]);
    QMF_SORTF(p[7], p[8]);
    QMF_SORTF(p[0], p[1]);
    QMF_SORTF(p[3], p[4]);
    QMF_SORTF(p[6], p[7]);
    QMF_SORTF(p[1], p[2]);
    QMF_SORTF(p[4], p[5]);
    QMF_SORTF(p[7], p[8]);
    QMF_SORTF(p[0], p[3]);
    QMF_SORTF(p[5], p[8]);
    QMF_SORTF(p[4], p[7]);
    QMF_SORTF(p[3], p[6]);
    QMF_SORTF(p[1], p[4]);
    QMF_SORTF(p[2], p[5]);
    QMF_SORTF(p[4], p[7]);
    QMF_SORTF(p[4], p[2]);
    QMF_SORTF(p[6], p[4]);
    QMF_SORTF(p[4], p[2]);
    return p[4];
}

/**
 * @brief 计算两个 int32_t 数组的逐元素差
 *
 * dest[i] = array1[i] - array2[i]
 *
 * @param dest   输出数组
 * @param array1 被减数数组
 * @param array2 减数数组
 * @param count  数组长度
 */
void arraySubInt32(int32_t* dest, const int32_t* array1, const int32_t* array2, const int count)
{
    for (int i = 0; i < count; i++) {
        dest[i] = array1[i] - array2[i];
    }
}

// ============================================================================
// Q12 定点数运算
// Q12 格式：1 位符号 + 3 位整数 + 12 位小数（实际是 1+15+16，用 int32_t 存储）
// ============================================================================

/**
 * @brief 将 Q12 定点数转换为百分比
 *
 * result = (100 * q) / 4096 = (100 * q) >> 12
 *
 * @param q Q12 定点数
 * @return  百分比（int16_t），如 q=2048 返回 50
 */
int16_t qPercent(const fix12_t q)
{
    return (100 * q) >> 12;
}

/**
 * @brief Q12 定点数与 int16_t 相乘
 *
 * result = (input * q) / 4096 = (input * q) >> 12
 * 用于将定点比例因子应用到整数值上。
 *
 * @param q     Q12 定点比例因子
 * @param input 整数值
 * @return      乘法结果（int16_t）
 */
int16_t qMultiply(const fix12_t q, const int16_t input)
{
    return (input * q) >> 12;
}

/**
 * @brief 用分子分母构造 Q12 定点数
 *
 * q = (num << 12) / den，即 q = 4096 * num / den
 *
 * @param num 分子
 * @param den 分母
 * @return    Q12 格式的定点数
 */
fix12_t qConstruct(const int16_t num, const int16_t den)
{
    return (num << 12) / den;
}
