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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


int32_t ext2_fsal_ln_hl(const char *src,
                        const char *dst)
{
    /**
     * TODO: implement the ext2_ln_hl command here ...
     * src and dst are the ln command arguments described in the handout.
     */
    if(DEBUG) printf("command: ln_hl from %s to %s \n", src, dst);
    
    char path[1024];
    strncpy(path, src, 1023);
    path[1023] = '\0'; 

    if(DEBUG) printf("command: cp from %s to %s \n", src, dst);
    char *first_part_src = malloc(sizeof(char) * 1024);
    char *second_part_src = malloc(sizeof(char) * 1024);
    split_in_half(src, &first_part_src, &second_part_src);
    int check_result_src;
    pthread_mutex_lock(&e_check_lock);
    // reason of the lock: please checkout ext2fsal_rm.c
    char *first_src = split(first_part_src);
    check_result_src = check_first_part(first_src, 2);  // 2 for root inode.
    pthread_mutex_unlock(&e_check_lock);

    if (check_result_src & (1 << 16)){
        if(DEBUG) printf("valid path: %s, final inode = %d \n", dst, check_result_src & ~(1 << 16));
    }
    else {
        if(DEBUG) printf("invalid path: %s, final inode = %d \n", dst, check_result_src);
        free(first_part_src);
        free(second_part_src);
        return check_result_src;
    }
    if(DEBUG) printf("\n--------------------------\n");


    // find src, save it's inode, create a dir entry for dst, dst->inode = src.inode.
    int parent_inode_src = check_result_src & ~(1 << 16);
    int src_inode;
    if ((src_inode = contains_file(parent_inode_src, second_part_src)) == -1){
        if(DEBUG) printf("src file cannot found.\n");
        free(first_part_src);
        free(second_part_src);
        return ENOENT;
    }

    char *first_part_dst = malloc(sizeof(char) * 1024);
    char *second_part_dst = malloc(sizeof(char) * 1024);
    split_in_half(dst, &first_part_dst, &second_part_dst);
    int check_result_dst;
    pthread_mutex_lock(&e_check_lock);
    // reason of the lock: please checkout ext2fsal_rm.c
    char *first_dst = split(first_part_dst);
    check_result_dst = check_first_part(first_dst, 2);  // 2 for root inode.
    pthread_mutex_unlock(&e_check_lock);
    if (check_result_dst & (1 << 16)){
        if(DEBUG) printf("valid path: %s, final inode = %d \n", dst, check_result_dst & ~(1 << 16));
    }
    else {
        if(DEBUG) printf("invalid path: %s, final inode = %d \n", dst, check_result_dst);
        free(first_part_src);
        free(second_part_src);
        free(first_part_dst);
        free(second_part_dst);
        return check_result_dst;
    }
    if(DEBUG) printf("\n--------------------------\n");
    
    int parent_inode_dst = check_result_dst & ~(1 << 16);
    if (contains_file(parent_inode_dst, second_part_dst) != -1){
        if(DEBUG) printf("dst file exist\n");
        free(first_part_src);
        free(second_part_src);
        free(first_part_dst);
        free(second_part_dst);
        return EEXIST;
    }

    // create a dir entry for dst under parent_inode_dst.

    int maybe_block;
    // check if we need a new block to store dir entry.
    pthread_mutex_lock(&(inodes_lock_array[parent_inode_dst])); // ZZY
    struct ext2_inode *parent_i = get_inode(parent_inode_dst);
    int target_block_num = parent_i->i_block[((parent_i->i_blocks) / 2) - 1];
    pthread_mutex_lock(&(block_lock_array[target_block_num])); // ZZY
    unsigned char *block = (unsigned char*)(disk + 1024 * target_block_num);
    int curr_byte = 0;
    struct ext2_dir_entry *dir;
    while(curr_byte != EXT2_BLOCK_SIZE){
        dir = (struct ext2_dir_entry *)(block + curr_byte);
        if (curr_byte + dir->rec_len == EXT2_BLOCK_SIZE){
            break;
        }
        curr_byte += dir->rec_len;
    }
    if(DEBUG) printf("over here: reclen = %d\n", dir->rec_len);
    int capacity = dir->rec_len - UPFOUR(8 + dir->name_len); // left capacity of this block
    int need = strlen(second_part_dst) + 8; // actucal need for the dir we need to add
    need = UPFOUR(need);
    if(DEBUG) printf("need: %d, cacacity: %d\n", need, capacity);
    if (need > capacity)
    {
        maybe_block = find_free_block();
        if (maybe_block == -1)
        {
            pthread_mutex_unlock(&(block_lock_array[target_block_num])); // ZZY
            pthread_mutex_unlock(&(inodes_lock_array[parent_inode_dst])); // ZZY
            free(first_part_src);
            free(second_part_src);
            free(first_part_dst);
            free(second_part_dst);
            return ENOSPC;
        }
        else{
            pthread_mutex_lock(&(block_lock_array[maybe_block])); // ZZY
            struct ext2_dir_entry *new_block = (struct ext2_dir_entry *)(disk + 1024 * maybe_block);
            memset(new_block, 0, 1024);
            new_block->name_len = min(strlen(second_part_dst), EXT2_NAME_LEN);
            new_block->rec_len = EXT2_BLOCK_SIZE;
            strncpy(new_block->name, second_part_dst, EXT2_NAME_LEN);
            new_block->inode = src_inode;  
            new_block->file_type = EXT2_FT_REG_FILE;
            pthread_mutex_unlock(&(block_lock_array[maybe_block])); // ZZY
            // update parent inode
            parent_i->i_blocks += 2;

            parent_i->i_block[parent_i->i_blocks / 2 - 1] = maybe_block;
            parent_i->i_size = parent_i->i_blocks / 2 * EXT2_BLOCK_SIZE;
        }
    }
    else{
        maybe_block = -1;
        dir->rec_len = UPFOUR(dir->name_len + 8); // update the rec_len
        struct ext2_dir_entry *new_dir = (void *)dir + dir->rec_len;
        new_dir->file_type = EXT2_FT_REG_FILE;
        new_dir->inode = src_inode;
        strncpy(new_dir->name, second_part_dst, EXT2_NAME_LEN);
        new_dir->name_len = min(strlen(second_part_dst), EXT2_NAME_LEN);
        new_dir->rec_len = capacity;
    }
    pthread_mutex_unlock(&(block_lock_array[target_block_num])); // ZZY
    pthread_mutex_unlock(&(inodes_lock_array[parent_inode_dst])); // ZZY

    // end
    pthread_mutex_lock(&(inodes_lock_array[src_inode]));
    struct ext2_inode *real_src_inode = get_inode(src_inode);
    real_src_inode->i_links_count++;
    pthread_mutex_unlock(&(inodes_lock_array[src_inode]));

    free(first_part_src);
    free(second_part_src);
    free(first_part_dst);
    free(second_part_dst);
    return 0;
}
