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

#ifndef CSC369_E2FS_H
#define CSC369_E2FS_H

#define DEBUG 0  // set to 0 on submission.

#include "ext2.h"
#include <pthread.h>
#include <time.h>

#define min(x, y) ((x >= y) ? y: x)
#define max(x, y) ((x >= y) ? x: y)
#define UPFOUR(a) ((((a) + 3)/4) * 4)
// python math.cell() reference from a3
#define CELL(a, b) (((a) + (b) - 1)/b)   


char *disk;  // disk image: giant char array.
struct ext2_super_block *sb;
struct ext2_group_desc *gd;
unsigned char *db_bm;  // data block bitmap
unsigned char *in_bm;  // inode bitmap

pthread_mutex_t e_check_lock;
pthread_mutex_t sb_lock;
pthread_mutex_t gd_lock;
pthread_mutex_t i_bitmap_lock;  // inode bitmap lock
pthread_mutex_t d_bitmap_lock;  // data block bitmap lock
pthread_mutex_t block_lock_array[128];  // array of lockes for each block
pthread_mutex_t inodes_lock_array[32];  // array of lockes for each inode.


char *split(const char *);
char find_last_char(const char *);
int db_occupied(int);
int in_occupied(int);
void split_in_half(const char *, char **, char **);
struct ext2_inode *get_inode(int);
int check_first_part(char *first, int inode);
int contains_file(int inode, char *filename);
void delete_file(int found_inode, int parent_inode);
int find_free_inode();
void set_free_inode(int index);
int find_free_block();
void set_free_block(int index);
int get_size(char *filename);  // return size of <filename> in byte.
#endif