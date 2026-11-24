/* fido_lease.h — ownership of the hidraw fd while a FIDO operation runs.
 * The event loop may revoke/replace the attached device while the worker thread uses job_fd.
 * Keep that fd alive until the worker reports completion, then close it before accepting results. */
#ifndef LORD_VAULT_FIDO_LEASE_H
#define LORD_VAULT_FIDO_LEASE_H

#include <unistd.h>

typedef struct {
    int device_fd;       /* currently attached fd, available to start a new operation */
    int job_fd;          /* stable fd borrowed by the worker while busy */
    int busy;
    int revoked;         /* the active job's device was revoked/replaced */
} FidoLease;

#define FIDO_LEASE_INIT { .device_fd = -1, .job_fd = -1 }

static inline int fido_lease_begin(FidoLease *l) {
    if (!l || l->busy || l->device_fd < 0) return -1;
    l->job_fd = l->device_fd;
    l->busy = 1;
    l->revoked = 0;
    return l->job_fd;
}

/* Takes ownership of fd (or -1 to revoke). A running job keeps its own fd until finish. */
static inline void fido_lease_attach(FidoLease *l, int fd) {
    if (!l) { if (fd >= 0) close(fd); return; }
    if (l->busy) l->revoked = 1;
    if (l->device_fd >= 0 && (!l->busy || l->device_fd != l->job_fd)) close(l->device_fd);
    l->device_fd = fd;
}

/* Called only after the worker has stopped using job_fd. Returns 1 if the result was revoked. */
static inline int fido_lease_finish(FidoLease *l) {
    if (!l || !l->busy) return 0;
    int revoked = l->revoked;
    if (revoked && l->job_fd >= 0) close(l->job_fd);
    l->job_fd = -1;
    l->busy = 0;
    l->revoked = 0;
    return revoked;
}

#endif
