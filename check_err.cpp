
#include <uv.h>
#include <stdio.h>
int main() {
    printf("%s: %s\n", uv_err_name(-4094), uv_strerror(-4094));
    return 0;
}
