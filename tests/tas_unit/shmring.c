#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "../testutils.h"
#include "../../proxy/shmring.h"


void test_reset_ring()
{
    void *base_addr;
    struct ring_buffer *ring;
    struct ring_header *hdr;
    size_t RING_SIZE = 100;

    base_addr = malloc(RING_SIZE);
    ring = shmring_init(base_addr, RING_SIZE);
    shmring_reset(ring, RING_SIZE);
    size_t hdr_size = sizeof(struct ring_header);
    hdr = ring->hdr_addr;
    
    test_assert("read_pos reset", hdr->read_pos == 0);
    test_assert("write_pos reset", hdr->write_pos == 0);
    test_assert("full set to not full", hdr->full == 0);
    test_assert("ring size set", hdr->ring_size == (RING_SIZE - hdr_size));

    free(base_addr);
}

void test_push_ring()
{
    int ret;
    void *base_addr, *msg;
    struct ring_buffer *ring;
    struct ring_header *hdr;
    size_t RING_SIZE = 100;

    base_addr = malloc(RING_SIZE);
    ring = shmring_init(base_addr, RING_SIZE);
    shmring_reset(ring, RING_SIZE);
    size_t hdr_size = sizeof(struct ring_header);
    hdr = ring->hdr_addr;

    size_t write_sz = 10;    
    msg = malloc(write_sz);

    ret = shmring_push(ring, msg, write_sz);
    test_assert("returned correct number of bytes written", 
            ret == write_sz);
    test_assert("write_pos updated", hdr->write_pos == write_sz);
    test_assert("read not updated", hdr->read_pos == 0);
    test_assert("ring is not full", hdr->full == 0);
    test_assert("hdr_size stayed the same", 
            hdr->ring_size == (RING_SIZE - hdr_size));

    free(base_addr);
    free(msg);
}

void test_push_full_ring()
{
    int ret;
    void *base_addr, *msg;
    struct ring_buffer *ring;
    struct ring_header *hdr;
    size_t RING_SIZE = 100;

    base_addr = malloc(RING_SIZE);
    ring = shmring_init(base_addr, RING_SIZE);
    shmring_reset(ring, RING_SIZE);
    size_t hdr_size = sizeof(struct ring_header);
    hdr = ring->hdr_addr;

    size_t write_sz = RING_SIZE - hdr_size;    
    msg = malloc(write_sz);

    ret = shmring_push(ring, msg, write_sz);
    test_assert("returned correct number of bytes written", 
            ret == write_sz);
    test_assert("write_pos updated", hdr->write_pos == 0);
    test_assert("read not updated", hdr->read_pos == 0);
    test_assert("ring is full", hdr->full == 1);
    test_assert("hdr_size stayed the same", 
            hdr->ring_size == (RING_SIZE - hdr_size));
    
    ret = shmring_push(ring, msg, 1);
    test_assert("write to full ring failed", ret == 0);
    test_assert("write_pos not updated", hdr->write_pos == 0);
    test_assert("read not updated", hdr->read_pos == 0);
    test_assert("ring is still full", hdr->full == 1);
    test_assert("hdr_size still stayed the same", 
            hdr->ring_size == (RING_SIZE - hdr_size));

    free(base_addr);
    free(msg);
}

void test_fragmented_push()
{
    int ret;
    void *base_addr, *msg, *dst;
    struct ring_buffer *ring;
    struct ring_header *hdr;
    size_t RING_SIZE = 100;

    base_addr = malloc(RING_SIZE);
    ring = shmring_init(base_addr, RING_SIZE);
    shmring_reset(ring, RING_SIZE);
    size_t hdr_size = sizeof(struct ring_header);
    hdr = ring->hdr_addr;

    size_t write_sz = RING_SIZE - hdr_size - 5;    
    msg = malloc(write_sz);
    ret = shmring_push(ring, msg, write_sz);
    
    test_assert("returned correct number of bytes written", 
            ret == write_sz);
    test_assert("write_pos updated", hdr->write_pos == write_sz);
    test_assert("read not updated", hdr->read_pos == 0);
    test_assert("ring is full", hdr->full == 0);
    test_assert("hdr_size stayed the same", 
            hdr->ring_size == (RING_SIZE - hdr_size));

    size_t read_sz = 10;
    dst = malloc(read_sz);
    shmring_pop(ring, dst, read_sz);

    size_t fwrite_sz = 8;
    assert(fwrite_sz < write_sz);
    ret = shmring_push(ring, msg, fwrite_sz);

    test_assert("returned correct number of bytes written for frag write", 
            ret == fwrite_sz);
    test_assert("write_pos updated for frag write", 
            hdr->write_pos == 3);
    test_assert("read not updated for frag write", hdr->read_pos == read_sz);
    test_assert("ring is not full", hdr->full == 0);
    test_assert("hdr_size stayed the same for frag write", 
            hdr->ring_size == (RING_SIZE - hdr_size));

    free(msg);
    free(dst);
}


void test_pop_ring()
{
    int ret;
    void *base_addr;
    struct ring_buffer *ring;
    struct ring_header *hdr;
    size_t RING_SIZE = 100;
    uint8_t write_msg[3] = {0, 1, 2};
    uint8_t *dst;

    base_addr = malloc(RING_SIZE);
    ring = shmring_init(base_addr, RING_SIZE);
    shmring_reset(ring, RING_SIZE);
    size_t hdr_size = sizeof(struct ring_header);
    hdr = ring->hdr_addr;

    size_t write_sz = 3;    
    shmring_push(ring, write_msg, write_sz);

    dst = malloc(write_sz);
    ret = shmring_pop(ring, dst, write_sz);

    test_assert("returned correct number of bytes read", ret == write_sz);
    test_assert("read did not update write pos", hdr->write_pos == write_sz);
    test_assert("read pos updated", hdr->read_pos == write_sz);
    test_assert("ring is not full", hdr->full == 0);
    test_assert("values are correct", 
            dst[0] == 0 && dst[1] == 1 && dst[2] == 2);
    test_assert("ring size stayed the same after read",
            hdr->ring_size == (RING_SIZE - hdr_size));

    free(base_addr);
    free(dst);
}

void test_pop_too_large()
{
    int ret;
    void *base_addr, *write_msg;
    struct ring_buffer *ring;
    struct ring_header *hdr;
    size_t RING_SIZE = 100;
    uint8_t *dst;

    base_addr = malloc(RING_SIZE);
    ring = shmring_init(base_addr, RING_SIZE);
    shmring_reset(ring, RING_SIZE);
    size_t hdr_size = sizeof(struct ring_header);
    hdr = ring->hdr_addr;

    size_t write_sz = 3;    
    write_msg = malloc(write_sz);
    shmring_push(ring, write_msg, write_sz);

    size_t read_sz = 4;
    dst = malloc(read_sz);
    ret = shmring_pop(ring, dst, read_sz);

    test_assert("returned no bytes read", ret == 0);
    test_assert("read did not update write pos", hdr->write_pos == write_sz);
    test_assert("read pos was not updated", hdr->read_pos == 0);
    test_assert("ring is not full", hdr->full == 0);
    test_assert("ring size stayed the same after read",
            hdr->ring_size == (RING_SIZE - hdr_size));

    free(base_addr);
    free(write_msg);
    free(dst);
}

void test_pop_empty_ring()
{
    int ret;
    void *base_addr, *write_msg;
    struct ring_buffer *ring;
    struct ring_header *hdr;
    size_t RING_SIZE = 100;
    size_t msg_sz = 4;
    uint8_t *dst;

    base_addr = malloc(RING_SIZE);
    ring = shmring_init(base_addr, RING_SIZE);
    shmring_reset(ring, RING_SIZE);
    size_t hdr_size = sizeof(struct ring_header);
    hdr = ring->hdr_addr;

    dst = malloc(msg_sz);
    ret = shmring_pop(ring, dst, msg_sz);

    test_assert("returned no bytes read", ret == 0);
    test_assert("read did not update write pos", hdr->write_pos == 0);
    test_assert("read pos was not updated", hdr->read_pos == 0);
    test_assert("ring is not full", hdr->full == 0);
    test_assert("ring size stayed the same after read",
            hdr->ring_size == (RING_SIZE - hdr_size));
 
    write_msg = malloc(msg_sz);
    shmring_push(ring, write_msg, msg_sz);
    shmring_pop(ring, dst, msg_sz);

    ret = shmring_pop(ring, dst, msg_sz);
    
    test_assert("second empty read returned no bytes", ret == 0);
    test_assert("second empty read did not update write pos",
            hdr->write_pos == msg_sz);
    test_assert("second empty read pos was not updated", 
            hdr->read_pos == msg_sz);
    test_assert("ring is not full after second empty read",
            hdr->full == 0);
    test_assert("ring size stayed the same after second empty read",
            hdr->ring_size == (RING_SIZE - hdr_size));

    free(base_addr);
    free(write_msg);
    free(dst);
}

void test_pop_ring_till_empty()
{
    int ret;
    void *base_addr, *msg, *dst;
    struct ring_buffer *ring;
    struct ring_header *hdr;
    size_t RING_SIZE = 100;

    base_addr = malloc(RING_SIZE);
    ring = shmring_init(base_addr, RING_SIZE);
    shmring_reset(ring, RING_SIZE);
    size_t hdr_size = sizeof(struct ring_header);
    hdr = ring->hdr_addr;

    size_t write_sz = RING_SIZE - hdr_size;    
    msg = malloc(write_sz);
    shmring_push(ring, msg, write_sz);

    dst = malloc(write_sz);
    ret = shmring_pop(ring, dst, write_sz);
    test_assert("returned correct number of bytes read", ret == write_sz);
    test_assert("read did not update write pos", hdr->write_pos == 0);
    test_assert("read pos updated", hdr->read_pos == 0);
    test_assert("ring is not full", hdr->full == 0);
    test_assert("ring size stayed the same after read",
            hdr->ring_size == (RING_SIZE - hdr_size));

    ret = shmring_pop(ring, dst, 1);
    test_assert("returned no read bytes", ret == 0);
    test_assert("second read did not update write pos", 
            hdr->write_pos == 0);
    test_assert("read pos not updated", hdr->read_pos == 0);
    test_assert("ring is still not full", hdr->full == 0);
    test_assert("ring size stayed the same after second read",
            hdr->ring_size == (RING_SIZE - hdr_size));

}

void test_fragmented_pop()
{
    int ret;
    void *base_addr, *jumbled_msg;
    struct ring_buffer *ring;
    struct ring_header *hdr;
    size_t RING_SIZE = 100;
    uint8_t write_msg[3] = {0, 1, 2};
    uint8_t *read_msg;
    uint8_t *dst;

    base_addr = malloc(RING_SIZE);
    ring = shmring_init(base_addr, RING_SIZE);
    shmring_reset(ring, RING_SIZE);
    size_t hdr_size = sizeof(struct ring_header);
    hdr = ring->hdr_addr;

    size_t write_sz = RING_SIZE - hdr_size - 1;   
    jumbled_msg = malloc(write_sz); 
    shmring_push(ring, write_msg, write_sz);
    dst = malloc(write_sz);
    shmring_pop(ring, dst, write_sz);
    shmring_push(ring, write_msg, 3);

    read_msg = malloc(3);
    ret = shmring_pop(ring, read_msg, 3);

    test_assert("returned correct number of bytes read", ret = 3);
    test_assert("read did not update write pos", hdr->write_pos == 2);
    test_assert("read pos got updated", hdr->read_pos == 2);
    test_assert("ring is not full", hdr->full == 0);
    test_assert("values are correct", 
            read_msg[0] == 0 && read_msg[1] == 1 && read_msg[2] == 2);
    test_assert("ring size statyed the same after read",
            hdr->ring_size == (RING_SIZE - hdr_size));

    free(base_addr);
    free(dst);
    free(read_msg);
    free(jumbled_msg);
}

void test_ring_is_empty()
{
    int ret;
    void *base_addr, *msg, *dst;
    struct ring_buffer *ring;
    size_t RING_SIZE = 100;

    base_addr = malloc(RING_SIZE);
    ring = shmring_init(base_addr, RING_SIZE);
    shmring_reset(ring, RING_SIZE);

    ret = shmring_is_empty(ring);
    test_assert("ring is empty after initialization", ret);

    size_t hdr_size = sizeof(struct ring_header);
    msg = malloc(RING_SIZE - hdr_size);
    dst = malloc(RING_SIZE - hdr_size);

    shmring_push(ring, msg, 10);
    ret = shmring_is_empty(ring);
    test_assert("ring is not empty after pushing one message", ret == 0);

    shmring_pop(ring, dst, 10);
    ret = shmring_is_empty(ring);
    test_assert("ring is empty after pushing and popping message", ret);

    shmring_push(ring, msg, RING_SIZE - hdr_size);
    ret = shmring_is_empty(ring);
    test_assert("ring is not empty when full", ret == 0);

    shmring_pop(ring, dst, RING_SIZE - hdr_size);
    ret = shmring_is_empty(ring);
    test_assert("ring is empty after being full and popped completely", ret);

    shmring_push(ring, msg, RING_SIZE - hdr_size);
    shmring_pop(ring, dst, 10);
    shmring_push(ring, msg, 5);
    ret = shmring_is_empty(ring);
    test_assert("ring is not empty when write_pos < read_pos", ret == 0);
}

void test_ring_get_freesz()
{
    int ret;
    void *base_addr, *msg, *dst;
    struct ring_buffer *ring;
    size_t RING_SIZE = 100;

    base_addr = malloc(RING_SIZE);
    ring = shmring_init(base_addr, RING_SIZE);
    shmring_reset(ring, RING_SIZE);
    size_t hdr_size = sizeof(struct ring_header);

    ret = shmring_get_freesz(ring);
    test_assert("get free size after initialization",
                ret == RING_SIZE - hdr_size);

    msg = malloc(RING_SIZE - hdr_size);
    dst = malloc(RING_SIZE - hdr_size);

    shmring_push(ring, msg, 10);
    ret = shmring_get_freesz(ring);
    test_assert("get free size after pushing one message", 
                ret == RING_SIZE - hdr_size - 10);

    shmring_pop(ring, dst, 10);
    ret = shmring_get_freesz(ring);
    test_assert("get free size after pushing and popping message", 
                ret == RING_SIZE - hdr_size);

    shmring_push(ring, msg, RING_SIZE - hdr_size);
    ret = shmring_get_freesz(ring);
    test_assert("get free size when full", ret == 0);

    shmring_pop(ring, dst, RING_SIZE - hdr_size);
    ret = shmring_get_freesz(ring);
    test_assert("get free size after being full and popped completely", 
                ret == RING_SIZE - hdr_size);

    shmring_push(ring, msg, RING_SIZE - hdr_size);
    shmring_pop(ring, dst, 10);
    shmring_push(ring, msg, 5);
    ret = shmring_get_freesz(ring);
    test_assert("get free size when write_pos < read_pos", ret == 5);
}

int main(int argc, char *argv[])
{
    int ret = 0;

    if (test_subcase("reset ring", test_reset_ring, NULL))
        ret = 1;

    if (test_subcase("push ring", test_push_ring, NULL))
        ret = 1;

    if (test_subcase("push full ring", test_push_full_ring, NULL))
        ret = 1;

    if (test_subcase("push frag write to ring", test_fragmented_push, NULL))
        ret = 1;
    
    if (test_subcase("pop ring", test_pop_ring, NULL))
        ret = 1;

    if (test_subcase("pop too large", test_pop_too_large, NULL))
        ret = 1;
    
    if (test_subcase("pop empty ring", test_pop_empty_ring, NULL))
        ret = 1;

    if (test_subcase("pop ring until empty", test_pop_ring_till_empty, NULL))
        ret = 1;

    if (test_subcase("pop frag write to ring", test_fragmented_pop, NULL))
        ret = 1;

    if (test_subcase("ring is empty check", test_ring_is_empty, NULL))
        ret = 1;

    if (test_subcase("get free size", test_ring_get_freesz, NULL))
        ret = 1;


    return ret;
}