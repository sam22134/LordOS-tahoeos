/* fido_lease_test.c — a revoked hidraw fd stays alive only until its worker finishes. */
#include "vault/fido_lease.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fails, total;
static void check(int ok, const char *what) {
    total++;
    if (!ok) { fails++; fprintf(stderr, "FAIL: %s\n", what); }
}
static int pipe_read(void) {
    int p[2];
    if (pipe(p)) return -1;
    close(p[1]);
    return p[0];
}
static int open_fd(int fd) { return fd >= 0 && fcntl(fd, F_GETFD) >= 0; }

int main(void) {
    FidoLease l = FIDO_LEASE_INIT;
    int old = pipe_read();
    check(old >= 0, "create active descriptor");
    fido_lease_attach(&l, old);
    check(fido_lease_begin(&l) == old, "worker borrows attached descriptor");
    fido_lease_attach(&l, -1);
    check(open_fd(old), "revocation does not close a descriptor under the worker");
    check(fido_lease_revoked(&l), "worker sees the atomic revocation signal");
    check(fido_lease_finish(&l) == 1, "worker result is marked revoked");
    errno = 0;
    check(!open_fd(old) && errno == EBADF, "revoked descriptor closes after worker completion");
    check(l.device_fd == -1 && !l.busy && !fido_lease_revoked(&l), "revoked lease has no attached device or stale signal");

    old = pipe_read();
    int next = pipe_read();
    check(old >= 0 && next >= 0, "create replacement descriptors");
    fido_lease_attach(&l, old);
    check(fido_lease_begin(&l) == old, "start before replacement");
    fido_lease_attach(&l, next);
    check(open_fd(old), "replacement preserves worker's active descriptor until finish");
    check(fido_lease_finish(&l) == 1, "replacement cancels active result");
    errno = 0;
    check(!open_fd(old) && errno == EBADF, "old descriptor closes after replacement finishes");
    check(open_fd(next) && l.device_fd == next, "new descriptor remains available for next operation");
    check(fido_lease_begin(&l) == next, "next operation uses replacement");
    check(fido_lease_finish(&l) == 0 && open_fd(next), "normal completion keeps attached descriptor");
    fido_lease_attach(&l, -1);
    errno = 0;
    check(!open_fd(next) && errno == EBADF, "idle revocation closes attached descriptor immediately");

    old = pipe_read();
    int mid = pipe_read();
    next = pipe_read();
    fido_lease_attach(&l, old);
    check(fido_lease_begin(&l) == old, "start before repeated replacements");
    fido_lease_attach(&l, mid);
    fido_lease_attach(&l, next);
    errno = 0;
    check(!open_fd(mid) && errno == EBADF, "superseded pending descriptor closes immediately");
    check(open_fd(old), "worker descriptor remains open through repeated replacements");
    check(fido_lease_finish(&l) == 1, "repeated replacement still revokes result");
    errno = 0;
    check(!open_fd(old) && errno == EBADF && open_fd(next), "finish closes only old worker descriptor");
    fido_lease_attach(&l, -1);

    printf("FIDO lease: %d checks, %d failures\n", total, fails);
    return fails ? 1 : 0;
}
