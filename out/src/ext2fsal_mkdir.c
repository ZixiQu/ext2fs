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
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int32_t ext2_fsal_mkdir(const char *path)
{
    /**
     * TODO: implement the ext2_mkdir command here ...
     * the argument path is the path to the directory that is to be created.
     */

     /* This is just to avoid compilation warnings, remove this line when you're done. */
    (void)path;
    if(DEBUG) printf("command: mkdir %s \n", path);
    char *good_path = malloc(sizeof(char) * 1024);
    if (find_last_char(path) == '/'){
        strncpy(good_path, path, strlen(path) - 1);
    }
    else {
        strcpy(good_path, path);
    }

    char *first_part = malloc(sizeof(char) * 1024);
    char *second_part = malloc(sizeof(char) * 1024);
    split_in_half(good_path, &first_part, &second_part);
    int check_result;
    pthread_mutex_lock(&e_check_lock);
    // reason of the lock: please checkout ext2fsal_rm.c
    char *first = split(first_part);
    check_result = check_first_part(first, 2);  // 2 for root inode.
    pthread_mutex_unlock(&e_check_lock);
    if (DEBUG)
    {
        if (check_result & (1 << 16)){
            if(DEBUG) printf("valid path: %s, final inode = %d \n", path, check_result & ~(1 << 16));
        }
        else {
            if(DEBUG) printf("invalid path: %s, final inode = %d \n", path, check_result);
            free(first_part);
            free(second_part);
            free(good_path);
            return check_result;
        }
        if(DEBUG) printf("\n--------------------------\n");
    }
    /*
    for mkdir, if the second part is a dir under it's parent: return EEXIST
      else if it's a file under it's parent dir, return EEXIST
      else if this does not even exist, NICE, make the dir.
    
    making a dir:
     - new inode
     - add "." and ".." (also need a new block)
     - set dir entry in parent (*may need a new block to store dir entry)
     so we need 1 new inode and 1 new (possibly 2) blocks. Need to check ahead.
    */
    if(DEBUG) printf("at the beginning\n");
    int parent_inode = check_result & ~(1 << 16);
    int ret_val = contains_file(parent_inode, second_part);
    if(DEBUG) printf("retval = %d\n", ret_val);
    if (ret_val != -1){
        if(DEBUG) printf("file[%s] already exist\n", second_part);
        free(first_part);
        free(second_part);
        free(good_path);
        return EEXIST;
    }
    if(DEBUG) printf("goes here\n");

    int new_inode = find_free_inode();
    if(DEBUG) printf("new_inode = %d\n", new_inode);
    if (new_inode == -1){
        // no free inode
        free(first_part);
        free(second_part);
        free(good_path);
        return ENOSPC;
    }
    // add in lock
    int necessary_block = find_free_block();
    if (necessary_block == -1){
        set_free_inode(new_inode);
        free(first_part);
        free(second_part);
        free(good_path);
        return ENOSPC;
    }
    if(DEBUG) printf("necessary_block: %d \n", necessary_block);
    int maybe_block;
    // check if we need a new block to store dir entry.
    struct ext2_inode *parent_i = get_inode(parent_inode);
    int target_block_num = parent_i->i_block[((parent_i->i_blocks) / 2) - 1];
    // add lock ZZY
    pthread_mutex_lock(&(block_lock_array[target_block_num]));
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
    int need = strlen(second_part) + 8; // actucal need for the dir we need to add
    need = UPFOUR(need);
    if(DEBUG) printf("need: %d, cacacity: %d\n", need, capacity);
    if (need > capacity)
    {
        maybe_block = find_free_block();
        if (maybe_block == -1)
        {
            set_free_block(necessary_block);
            set_free_inode(new_inode);
            // add unlock ZZY
            pthread_mutex_unlock(&(block_lock_array[target_block_num]));
            free(first_part);
            free(second_part);
            free(good_path);
            return ENOSPC;
        }
        else{
            // maybe deallock here ZZY
            pthread_mutex_lock(&(block_lock_array[maybe_block]));
            struct ext2_dir_entry *new_block = (struct ext2_dir_entry *)(disk + 1024 * maybe_block);
            memset(new_block, 0, 1024);
            new_block->name_len = min(strlen(second_part), EXT2_NAME_LEN);
            new_block->rec_len = EXT2_BLOCK_SIZE;
            strncpy(new_block->name, second_part, EXT2_NAME_LEN);
            new_block->inode = new_inode;
            new_block->file_type = EXT2_FT_DIR;
            // update parent inode
            parent_i->i_blocks += 2;

            parent_i->i_block[parent_i->i_blocks / 2 - 1] = maybe_block;
            parent_i->i_size = parent_i->i_blocks / 2 * EXT2_BLOCK_SIZE;
            // add unlick here ZZY
            pthread_mutex_unlock(&(block_lock_array[maybe_block]));
            pthread_mutex_unlock(&(block_lock_array[target_block_num]));
        }
    }
    else{
        maybe_block = -1;
        dir->rec_len = UPFOUR(dir->name_len + 8); // update the rec_len
        struct ext2_dir_entry *new_dir = (void *)dir + dir->rec_len;
        new_dir->file_type = EXT2_FT_DIR;
        new_dir->inode = new_inode;
        strncpy(new_dir->name, second_part, EXT2_NAME_LEN);
        new_dir->name_len = min(strlen(second_part), EXT2_NAME_LEN);
        new_dir->rec_len = capacity;
        // add unlock here ZZY
        pthread_mutex_unlock(&(block_lock_array[target_block_num]));
    }
    // update the inode
    // add lock here ZZY
    pthread_mutex_lock(&(inodes_lock_array[new_inode]));
    struct ext2_inode *real_inode = get_inode(new_inode); 
    memset(real_inode, 0, sizeof(struct ext2_inode));
    real_inode->i_mode |= EXT2_S_IFDIR;
    real_inode->i_links_count = 2;
    real_inode->i_block[0] = necessary_block;
    real_inode->i_blocks = 2;
    real_inode->i_ctime = (unsigned int) time(NULL);
    real_inode->i_size = (real_inode->i_blocks / 2) * EXT2_BLOCK_SIZE;
    pthread_mutex_unlock(&(inodes_lock_array[new_inode])); // ZZY

    // hard code: adding . and .. to folder.
    pthread_mutex_lock(&(block_lock_array[necessary_block])); // ZZY
    struct ext2_dir_entry *dot = (struct ext2_dir_entry *)(disk + necessary_block * 1024);
    memset(dot, 0 , EXT2_BLOCK_SIZE);
    dot->inode = new_inode;
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    strcpy(dot->name, ".");
    dot->rec_len = (UPFOUR(8 + 1));
    if(DEBUG) printf("dot->rec_len = %d\n", dot->rec_len);

    struct ext2_dir_entry *dotdot = (struct ext2_dir_entry *)(disk + necessary_block * 1024 + dot->rec_len);
    dotdot->inode = parent_inode;
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    strcpy(dotdot->name, "..");
    dotdot->rec_len = 1024 - dot->rec_len;
    if(DEBUG) printf("dotdot->rec_len = %d\n", dotdot->rec_len);

    pthread_mutex_unlock(&(block_lock_array[necessary_block])); // ZZY

    // update parnet inode link_count.
    pthread_mutex_lock(&(inodes_lock_array[parent_inode]));
    struct ext2_inode *real_parent_inode = get_inode(parent_inode);
    real_parent_inode->i_links_count++;
    pthread_mutex_unlock(&(inodes_lock_array[parent_inode]));

    if(DEBUG) printf("return value [%d] \n", 0);
    free(first_part);
    free(second_part);
    free(good_path);
    return 0;
}