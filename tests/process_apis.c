#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <unistd.h>

extern void __bennett_rt_prepare_process(int, char **, char **);
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int empty(const char *p) { return p && !*p; }
static int check_locale(void) {
    struct lconv *l = localeconv();
    CHECK(l && l->decimal_point && l->decimal_point[0] == '.' && !l->decimal_point[1]);
    CHECK(empty(l->thousands_sep) && empty(l->grouping));
    CHECK(empty(l->int_curr_symbol) && empty(l->currency_symbol));
    CHECK(empty(l->mon_decimal_point) && empty(l->mon_thousands_sep) && empty(l->mon_grouping));
    CHECK(empty(l->positive_sign) && empty(l->negative_sign));
    CHECK(l->int_frac_digits == CHAR_MAX && l->frac_digits == CHAR_MAX);
    CHECK(l->p_cs_precedes == CHAR_MAX && l->p_sep_by_space == CHAR_MAX);
    CHECK(l->n_cs_precedes == CHAR_MAX && l->n_sep_by_space == CHAR_MAX);
    CHECK(l->p_sign_posn == CHAR_MAX && l->n_sign_posn == CHAR_MAX);
    CHECK(l->int_p_cs_precedes == CHAR_MAX && l->int_p_sep_by_space == CHAR_MAX);
    CHECK(l->int_n_cs_precedes == CHAR_MAX && l->int_n_sep_by_space == CHAR_MAX);
    CHECK(l->int_p_sign_posn == CHAR_MAX && l->int_n_sign_posn == CHAR_MAX);
    return 0;
}
int main(void) {
    CHECK(getpagesize() == 4096);
    int result = check_locale(); if (result) return result;
    static char *empty_vector[] = {0};
    errno = 42;
    __bennett_rt_prepare_process(0, empty_vector, empty_vector);
    CHECK(errno == 0 && getpagesize() == 4096);
    return check_locale();
}
