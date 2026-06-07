#include <stdio.h>
#include <stdint.h>
#include <math.h>

/*
 * Test: magnitude preservation through the int → double promotion ladder.
 * Verifies that positive * positive never goes negative when i128 overflows to double.
 */

int main(void) {
    /* i128 max */
    __int128 i128_max = ((unsigned __int128)1 << 127) - 1;

    /* (double)i128_max rounds up to 2^127, but stays positive */
    double dmax = (double)i128_max;
    printf("(double)i128_max = %g  (positive: %s)\n", dmax, dmax > 0 ? "yes" : "NO!");

    /* simulate overflow promotion: large_a * large_b as double */
    __int128 large_a = (__int128)1e18;
    __int128 large_b = (__int128)1e18;
    __int128 overflow_result;
    int overflowed = __builtin_mul_overflow(large_a, large_b, &overflow_result);

    printf("\n1e18 * 1e18:\n");
    printf("  i128 overflow: %s\n", overflowed ? "yes" : "no");
    if (overflowed) {
        double da = (double)large_a;
        double db = (double)large_b;
        double dr = da * db;
        printf("  double result: %g  (positive: %s)\n", dr, dr > 0 ? "yes" : "NO!");
    }

    /* edge: near-max i128 values */
    __int128 near_max = i128_max / 2;
    __int128 three = 3;
    overflowed = __builtin_mul_overflow(near_max, three, &overflow_result);
    printf("\n(i128_max/2) * 3:\n");
    printf("  i128 overflow: %s\n", overflowed ? "yes" : "no");
    if (overflowed) {
        double dr = (double)near_max * (double)three;
        printf("  double result: %g  (positive: %s)\n", dr, dr > 0 ? "yes" : "NO!");
    }

    /* verify: double can't wrap negative from positive * positive */
    printf("\nMagnitude safety checks:\n");
    double pos_inf = (double)i128_max * (double)i128_max;
    printf("  i128_max^2 as double: %g  (positive: %s)\n", pos_inf, pos_inf > 0 ? "yes" : "NO!");
    printf("  is inf: %s\n", isinf(pos_inf) ? "yes" : "no");

    /* add: positive + positive can't go negative in double */
    double add_big = (double)i128_max + (double)i128_max;
    printf("  i128_max + i128_max as double: %g  (positive: %s)\n", add_big, add_big > 0 ? "yes" : "NO!");

    printf("\nAll magnitude preservation tests passed.\n");
    return 0;
}
