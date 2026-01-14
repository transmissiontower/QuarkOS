#include "lib/math.h"
#include "lib/stdint.h"

static const int32_t sin_lut_90[] = { 0, 1143, 2287, 3429, 4571, 5711, 6850, 7986, 9120, 10252, 11380, 12504, 13625,
    14742, 15854, 16961, 18064, 19160, 20251, 21336, 22414, 23486, 24550, 25606, 26655, 27696, 28729, 29752, 30767,
    31772, 32768, 33753, 34728, 35693, 36647, 37589, 38521, 39440, 40347, 41243, 42125, 42995, 43852, 44695, 45525,
    46340, 47142, 47930, 48702, 49460, 50203, 50931, 51643, 52339, 53019, 53683, 54331, 54963, 55577, 56175, 56755,
    57319, 57864, 58393, 58903, 59395, 59870, 60326, 60763, 61183, 61583, 61965, 62328, 62672, 62997, 63302, 63589,
    63856, 64103, 64331, 64540, 64729, 64898, 65047, 65176, 65286, 65376, 65446, 65496, 65526, 65536 };

double fabs(double x) {
    return (x < 0) ? -x : x;
}

double floor(double x) {
    long xi = (long)x;
    return (x < xi) ? xi - 1 : x;
}

double fmod(double x, double y) {
    return x - (int)(x / y) * y;
}

int32_t i_fmod(int32_t x, int32_t y) {
    return x - (int)(x / y) * y;
}

double sqrt(double x) {
    if (x < 0)
        return -1.0;
    if (x == 0)
        return 0;

    double z = x;
    for (int i = 0; i < 10; i++) {
        z = z - (z * z - x) / (2 * z);
    }
    return z;
}

double sin(double x) {
    x = fmod(x, 2 * PI);
    if (x < -PI)
        x += 2 * PI;
    if (x > PI)
        x -= 2 * PI;

    double res = 0;
    double term = x;
    int k = 1;

    for (int i = 1; i <= 10; i++) {
        res += term;
        term *= -1 * x * x / ((2 * k) * (2 * k + 1));
        k++;
    }
    return res;
}

int32_t i_sin(int32_t x) {
    x %= 360;
    if (x < 0)
        x += 360;
    if (x <= 90)
        return sin_lut_90[x];
    if (x <= 180)
        return sin_lut_90[180 - x];
    if (x <= 270)
        return -sin_lut_90[x - 180];
    return -sin_lut_90[360 - x];
}

double cos(double x) {
    return sin(x + PI / 2);
}

int32_t i_cos(int32_t x) {
    return i_sin(x + 90);
}
