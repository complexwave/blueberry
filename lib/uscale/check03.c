#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "uscale.c"
int main(void) {
    double a = 0.3;
    double b = 0.30000000000000004;
    uint64_t ab, bb;
    memcpy(&ab, &a, 8); memcpy(&bb, &b, 8);
    printf("0.3                  = 0x%016llx\n", (unsigned long long)ab);
    printf("0.30000000000000004  = 0x%016llx\n", (unsigned long long)bb);
    printf("same bits? %s\n", ab == bb ? "YES" : "NO");
    uscale_digits r = uscale_dtoa_short(a);
    printf("dtoa_short(0.3): d=%llu p=%d\n", (unsigned long long)r.d, r.p);

    double c = 0.1;
    double d2 = 0.2;
    double sum = c + d2;
    uint64_t cb, db, sb;
    memcpy(&cb, &c, 8); memcpy(&db, &d2, 8); memcpy(&sb, &sum, 8);
    printf("\n0.1      = 0x%016llx\n", (unsigned long long)cb);
    printf("0.2      = 0x%016llx\n", (unsigned long long)db);
    printf("0.1+0.2  = 0x%016llx\n", (unsigned long long)sb);
    printf("== 0.3?  %s (0x%016llx)\n", sb == ab ? "YES" : "NO", (unsigned long long)ab);
    printf("== 0.30000000000000004?  %s (0x%016llx)\n", sb == bb ? "YES" : "NO", (unsigned long long)bb);

    uscale_digits r1 = uscale_dtoa_short(c);
    uscale_digits r2 = uscale_dtoa_short(d2);
    uscale_digits r3 = uscale_dtoa_short(sum);
    printf("\ndtoa_short(0.1):     d=%llu p=%d\n", (unsigned long long)r1.d, r1.p);
    printf("dtoa_short(0.2):     d=%llu p=%d\n", (unsigned long long)r2.d, r2.p);
    printf("dtoa_short(0.1+0.2): d=%llu p=%d\n", (unsigned long long)r3.d, r3.p);

    uscale_digits f1 = uscale_dtoa_fixed(c, 5);
    uscale_digits f2 = uscale_dtoa_fixed(d2, 5);
    uscale_digits f3 = uscale_dtoa_fixed(sum, 5);
    uscale_digits fa = uscale_dtoa_fixed(a, 5);
    printf("\ndtoa_fixed(0.1, 5):     d=%llu p=%d\n", (unsigned long long)f1.d, f1.p);
    printf("dtoa_fixed(0.2, 5):     d=%llu p=%d\n", (unsigned long long)f2.d, f2.p);
    printf("dtoa_fixed(0.1+0.2, 5): d=%llu p=%d\n", (unsigned long long)f3.d, f3.p);
    printf("dtoa_fixed(0.3, 5):     d=%llu p=%d\n", (unsigned long long)fa.d, fa.p);
    check_shared();
    return 0;
}

// appended: check 1000000.0000001
void check_shared(void) {
    double x = 1000000.0000001;
    uscale_digits r = uscale_dtoa_short(x);
    printf("\n1000000.0000001:\n");
    printf("  actual double: %.17g\n", x);
    printf("  d=%llu p=%d nd=%d\n", (unsigned long long)r.d, r.p, uscale_digits_count(r.d));
}
