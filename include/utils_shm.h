#ifndef UTILS_SHM_H_
#define UTILS_SHM_H_

void *util_create_shmsiszed(const char *name, size_t size, void *addr, int *pfd);
void util_destroy_shm(const char *name, size_t size, void *addr);
void *util_create_shmsiszed_huge(const char *name, size_t size,
    void *addr, int *pfd, char *flexnic_huge_prefix);
void util_destroy_shm_huge(const char *name, size_t size, void *addr,
    char *flexnic_huge_prefix);

#endif // ndef UTILS_SHM_H_
