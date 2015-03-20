#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    const char msg[] = "not implemented\n";
    write(2, msg, sizeof(msg));
    return 1;
}
