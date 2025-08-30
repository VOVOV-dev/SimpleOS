// demo_prints.c
// Show multiple ways to "print", from stdio to raw syscalls.
// Target: Linux. Tested for x86_64 inline-asm path (guarded by #if).

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>       // write
#include <sys/syscall.h>  // SYS_write, SYS_writev
#include <sys/uio.h>      // writev
#include <errno.h>

// -----------------------------
// 1) High-level stdio: buffered
// -----------------------------
static void print_printf(const char *s) {
    // printf does formatting + buffering; only flushes on '\n' to TTY (line-buffered)
    printf("[printf ] %s\n", s);
}

static void print_fputs(const char *s) {
    // fputs writes to FILE* buffer, no extra newline; flush explicitly
    fputs("[fputs  ] ", stdout);
    fputs(s, stdout);
    fputc('\n', stdout);
    fflush(stdout); // show students where the buffer actually flushes
}

static void print_fwrite(const char *s) {
    // fwrite can show partial-buffering behavior; here we flush
    const char *prefix = "[fwrite ] ";
    fwrite(prefix, 1, strlen(prefix), stdout);
    fwrite(s, 1, strlen(s), stdout);
    fwrite("\n", 1, 1, stdout);
    fflush(stdout);
}

// ------------------------------------------------------
// 2) POSIX write(2): unbuffered, direct kernel syscall
//    (but still via libc wrapper). Demonstrate EINTR
//    and short writes with a tiny "write_all" helper.
// ------------------------------------------------------
static ssize_t write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t nleft = len;
    while (nleft > 0) {
        ssize_t n = write(fd, p, nleft);
        if (n < 0) {
            if (errno == EINTR) continue; // interrupted by signal; retry
            return -1;                     // real error
        }
        if (n == 0) break; // shouldn't happen for regular fds, but bail
        p += n;
        nleft -= (size_t)n;
    }
    return (ssize_t)(len - nleft);
}

static void print_write_checked(const char *s) {
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "[write  ] %s\n", s);

    if (write_all(STDOUT_FILENO, buf, (size_t)n) < 0) {
        // avoid printf here to keep the path pure; use write to stderr
        const char *msg = "write_all failed\n";
        write(STDERR_FILENO, msg, strlen(msg));
    }
}

// ----------------------------------------------------------
// 3) syscall(2) wrapper: bypass libc's write() wrapper layer
// ----------------------------------------------------------
static void print_syscall(const char *s) {
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "[syscall] %s\n", s);

    // long syscall(long number, ...);
    long r = syscall(SYS_write, (long)STDOUT_FILENO, (long)buf, (long)n);
    if (r < 0) {
        const char *msg = "syscall(SYS_write) failed\n";
        write(STDERR_FILENO, msg, strlen(msg));
    }
}

// ----------------------------------------------------------
// 4) writev(2): "gather write" multiple buffers in one syscall
// ----------------------------------------------------------
static void print_writev_demo(const char *s) {
    struct iovec iov[3];
    const char *a = "[writev ] ";
    const char *b = "\n";

    iov[0].iov_base = (void *)a; iov[0].iov_len = strlen(a);
    iov[1].iov_base = (void *)s; iov[1].iov_len = strlen(s);
    iov[2].iov_base = (void *)b; iov[2].iov_len = 1;

    ssize_t r = writev(STDOUT_FILENO, iov, 3);
    if (r < 0) {
        const char *msg = "writev failed\n";
        write(STDERR_FILENO, msg, strlen(msg));
    }
}

// ------------------------------------------------------------------
// 5) Raw inline-assembly syscall (x86_64 Linux): ultimate "no libc"
//    NOTE: This is architecture-specific and guarded. Great for OS!
// ------------------------------------------------------------------
#if defined(__x86_64__) && defined(__linux__)
static ssize_t raw_syscall_write_x86_64(int fd, const void *buf, size_t len) {
    // On x86_64 Linux:
    //   rax = syscall number (SYS_write)
    //   rdi = fd, rsi = buf, rdx = len
    //   syscall instruction clobbers rcx, r11
    long ret;
    register long rax __asm__("rax") = SYS_write;
    register long rdi __asm__("rdi") = (long)fd;
    register long rsi __asm__("rsi") = (long)buf;
    register long rdx __asm__("rdx") = (long)len;
    __asm__ volatile(
        "syscall"
        : "+r"(rax)                 // rax in/out
        : "r"(rdi), "r"(rsi), "r"(rdx)
        : "rcx", "r11", "memory"
    );
    ret = rax; // kernel return value in rax
    // In raw syscalls, negative return values encode -errno
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (ssize_t)ret;
}

static void print_asm_syscall(const char *s) {
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "[asm    ] %s\n", s);
    if (raw_syscall_write_x86_64(STDOUT_FILENO, buf, (size_t)n) < 0) {
        const char *msg = "inline-asm syscall write failed\n";
        write(STDERR_FILENO, msg, strlen(msg));
    }
}
#else
static void print_asm_syscall(const char *s) {
    (void)s;
    const char *msg = "[asm    ] (inline-asm path not available on this arch)\n";
    write(STDOUT_FILENO, msg, strlen(msg));
}
#endif

// --------------------------------------
// Small demo of stdio buffering behavior
// --------------------------------------
static void show_stdio_buffering(void) {
    // Make stdout fully buffered to illustrate that nothing appears
    // until we fflush or print a newline (to a TTY it’s typically line buffered)
    setvbuf(stdout, NULL, _IOFBF, 1 << 12); // 4 KiB buffer
    fputs("[buffer ] this is buffered ... ", stdout);
    // No newline yet; force a flush:
    fflush(stdout);
    fputs("and now it's flushed!\n", stdout);
}

int main(void) {
    const char *msg = "Hello from different printing paths";

    // 1) High-level stdio (buffered, formatting)
    print_printf(msg);
    print_fputs(msg);
    print_fwrite(msg);

    // 2) POSIX write with robust loop for short/EINTR
    print_write_checked(msg);

    // 3) syscall() wrapper
    print_syscall(msg);

    // 4) writev: multiple buffers in one syscall
    print_writev_demo(msg);

    // 5) Raw inline-assembly syscall (x86_64 Linux)
    print_asm_syscall(msg);

    // Bonus: show buffering explicitly
    show_stdio_buffering();

    return 0;
}
