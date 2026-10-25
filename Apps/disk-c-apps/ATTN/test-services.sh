#!/bin/sh
# Build/run the native adapter against the actual shared transport.
set -eu
cd "$(dirname "$0")"
mkdir -p build
cc -std=c11 -O2 -Wno-deprecated-non-prototype \
    -Wno-implicit-function-declaration -include stdlib.h -x c \
    -I ../COMMON attnhost.c ../COMMON/DXSVC.C ../COMMON/tests/SVCCORE.C \
    -o build/attnhost
./build/attnhost ATTN.IN

{
    sed '/^int main(int argc, char \*argv\[\])/,$d' attnhost.c
    cat <<'EOF'
#include <assert.h>
int main(void)
{
    unsigned char bytes[4], bad[2] = {2, 3};
    uint32_t elapsed;
    assert(svc_access_stopwatch(0, 0, NULL) == 0 && service_status == 0);
    stopwatch_started.tv_sec -= 17;
    assert(svc_access_stopwatch(0, 2, bytes) == 4);
    elapsed = bytes[0] | ((uint32_t)bytes[1] << 8) |
              ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    assert(elapsed >= 17000 && elapsed < 18000);
    assert(svc_access_stopwatch(0, 1, bytes) == 4 && bytes[0] == 17 && bytes[1] == 0);
    assert(svc_call_service(SERVICE_GROUP_STOPWATCHES, bad, 2, bytes, 4) == -4);
    assert(service_status == 0);
    puts("native dedicated stopwatch, compatible framing and error-reset tests passed");
    return 0;
}
EOF
} | cc -std=c11 -O2 -Wno-deprecated-non-prototype \
    -Wno-implicit-function-declaration -include stdlib.h -iquote . \
    -I ../COMMON -x c - ../COMMON/DXSVC.C ../COMMON/tests/SVCCORE.C \
    -o build/stopwatch-service-test
./build/stopwatch-service-test
