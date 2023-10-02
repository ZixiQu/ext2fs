/*
 *------------
 * This code is provided solely for the personal and private use of
 * students taking the CSC369H5 course at the University of Toronto.
 * Copying for purposes other than this use is expressly prohibited.
 * All forms of distribution of this code, whether as given or with
 * any changes, are expressly prohibited.
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019 MCS @ UTM
 * -------------
 */

#include "ext2fsal.h"
#include "e2fs.h"
#include <stdio.h>
// for open()
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extern char *disk;

void ext2_fsal_init(const char* image)
{
    /**
     * TODO: Initialization tasks, e.g., initialize synchronization primitives used,
     * or any other structures that may need to be initialized in your implementation,
     * open the disk image by mmap-ing it, etc.
     */
    printf("init: image = %s\n", image);  // image is the name of the file.
    int fd = open(image, O_RDWR);
    if (fd == -1){
        perror("file name not found or open() failed \n");
        exit(1);
    }

    disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    pthread_mutex_init(&sb_lock, NULL);
    pthread_mutex_init(&gd_lock, NULL);
    pthread_mutex_init(&i_bitmap_lock, NULL);
    pthread_mutex_init(&d_bitmap_lock, NULL);
    pthread_mutex_init(&e_check_lock, NULL);
    int i;
    for (i = 0; i < 128; i++){
        pthread_mutex_init(&(block_lock_array[i]), NULL);
    }
    for (i = 0; i < 32; i++){
        pthread_mutex_init(&(inodes_lock_array[i]), NULL);
    }
    sb = (struct ext2_super_block *)(disk + 1024 * 1);
    gd = (struct ext2_group_desc *)(disk + 1024 * 2);
    db_bm = (unsigned char*)(disk + 1024 * 3);
    in_bm = (unsigned char*)(disk + 1024 * 4);
}

void ext2_fsal_destroy()
{
    /**
     * TODO: Cleanup tasks, e.g., destroy synchronization primitives, munmap the image, etc.
     */

    munmap(disk, 128 * 1024);

    pthread_mutex_destroy(&sb_lock);
    pthread_mutex_destroy(&gd_lock);
    pthread_mutex_destroy(&i_bitmap_lock);
    pthread_mutex_destroy(&d_bitmap_lock);
    pthread_mutex_destroy(&e_check_lock);
    int i;
    for (i = 0; i < 128; i++){
        pthread_mutex_destroy(&(block_lock_array[i]));
    }
    for (i = 0; i < 32; i++){
        pthread_mutex_destroy(&(inodes_lock_array[i]));
    }
}