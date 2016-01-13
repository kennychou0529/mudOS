/*
** A simple and fast generic string hasher based on Peter K. Pearson's
** article in CACM 33-6, pp. 677. 这是一种字符串哈希方式。
*/
#define EDIT_SOURCE
#define NO_SOCKETS
#define NO_OPCODES

#include "std.h"
#include "hash.h"
/* 都是一个无符号char 就可以存的 */
static int T[] =
{
    1, 87, 49, 12, 176, 178, 102, 166, 121, 193, 6, 84, 249, 230, 44, 163,
    14, 197, 213, 181, 161, 85, 218, 80, 64, 239, 24, 226, 236, 142, 38, 200,
    110, 177, 104, 103, 141, 253, 255, 50, 77, 101, 81, 18, 45, 96, 31, 222,
    25, 107, 190, 70, 86, 237, 240, 34, 72, 242, 20, 214, 244, 227, 149, 235,
    97, 234, 57, 22, 60, 250, 82, 175, 208, 5, 127, 199, 111, 62, 135, 248,
    174, 169, 211, 58, 66, 154, 106, 195, 245, 171, 17, 187, 182, 179, 0, 243,
    132, 56, 148, 75, 128, 133, 158, 100, 130, 126, 91, 13, 153, 246, 216, 219,
    119, 68, 223, 78, 83, 88, 201, 99, 122, 11, 92, 32, 136, 114, 52, 10,
    138, 30, 48, 183, 156, 35, 61, 26, 143, 74, 251, 94, 129, 162, 63, 152,
    170, 7, 115, 167, 241, 206, 3, 150, 55, 59, 151, 220, 90, 53, 23, 131,
    125, 173, 15, 238, 79, 95, 89, 16, 105, 137, 225, 224, 217, 160, 37, 123,
    118, 73, 2, 157, 46, 116, 9, 145, 134, 228, 207, 212, 202, 215, 69, 229,
    27, 188, 67, 124, 168, 252, 42, 4, 29, 108, 21, 247, 19, 205, 39, 203,
    233, 40, 186, 147, 198, 192, 155, 33, 164, 191, 98, 204, 165, 180, 117, 76,
    140, 36, 210, 172, 41, 54, 159, 8, 185, 232, 113, 196, 231, 47, 146, 120,
    51, 65, 28, 144, 254, 221, 93, 189, 194, 139, 112, 43, 71, 109, 184, 209,
};

INLINE int				/* 确实会处理前maxn+1个字节 */
hashstr(char * s,		/* string to hash */
        int maxn,		/* maximum number of chars to consider */
        int hashs)		/* 控制哈希成多少位的，看返回值 */
{
    register unsigned int h;
    register unsigned char *p;

    h = (unsigned char) *s;
    if (h) {
        if (hashs > 256) {	/* 超过一个字节 */
            register int oh = T[(unsigned char) *s];

            for (p = (unsigned char *) s + 1; *p && p <= (unsigned char *) s + maxn; p++) {
                h = T[h ^ *p];
                oh = T[oh ^ *p];
            }
            h |= (oh << 8);		/* 变成16位 */
        } else{	/* 小于8位的，不需要算高8位了 */
            for (p = (unsigned char *) s + 1; *p && p <= (unsigned char *) s + maxn; p++)
                h = T[h ^ *p];
		}
    }
    return (int) (h % hashs);
}
/*
 * whashstr is faster than hashstr, but requires an even power-of-2 table size
 * Taken from Amylaars driver.	这个比上面hashstr更快，但需要一个偶数，是2的幂的表大小
 * 哈希方式是: 哈希结果是16位的有效整数，高8位与低8位分别进行同样的处理，区别在于低8位
 * 只是在高8位的基础上加了1而已，但是处理出来可能非常不一样。
 */
INLINE int
whashstr(char *  s, int  maxn)		/* 好像处理多了1个字节，看下面循环次数 */
{
    register unsigned char oh, h;
    register unsigned char *p;
    register int i;

    if (!*s)
        return 0;
    p = (unsigned char *) s;
    oh = T[*p];					/* 取出字符串的首个字节，伪随机出另一个数字 */
    h = (*(p++) + 1) & 0xff;	/* 做了三步工作：加1，再自加，再取余 */
    for (i = maxn - 1; *p && --i >= 0; ) {	/* 从第2个字节开始到下标为maxn，逐个字节处理 */
        oh = T[oh ^ *p];					/* 字节伪随机 */
        h = T[h ^ *p];
		p++;
    }

    return (oh << 8) + h;	/* 哈希成16位的哦！ */
}
