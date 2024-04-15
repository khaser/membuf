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

    const char* wrong_ptr = (char*) 0x7fffffff;
    if (write(fd, wrong_ptr, 1) == -1) {
        if (errno == EFAULT) {
            return 0;
        } else {
            printf("Write error: %d, but expected EFAULT\n", errno);
            return 2;
        }
    } else {
        printf("Write error expected, but not occured\n");
        return 2;
    }

    if (close(fd) == -1) {
        printf("Close error\n");
        return 1;
    }

    return 0;
}
