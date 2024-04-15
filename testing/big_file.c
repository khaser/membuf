#include "fcntl.h"
#include "unistd.h"
#include "stdio.h"
#include "errno.h"
#include "stdlib.h"

int main() {
    char dev_path[] = "/dev/klog";
    int fd;
    if ((fd = open(dev_path, O_WRONLY)) == -1) {
        printf("Open error: %d\n", errno);
        return 1;
    }

    const int N = 4096;
    const char* msg = malloc(N);
    if (write(fd, msg, N) == -1) {
        printf("Write error: %d\n", errno);
        return 2;
    }
    if (close(fd) == -1) {
        printf("Close error\n");
        return 1;
    }

    return 0;
}
