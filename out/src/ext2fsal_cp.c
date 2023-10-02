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


int32_t ext2_fsal_cp(const char *src,
                     const char *dst)
{
    /**
     * TODO: implement the ext2_cp command here ...
     * Arguments src and dst are the cp command arguments described in the handout.
     */

    char path[1024];
    strncpy(path, src, 1023);
    path[1023] = '\0'; 

    if(DEBUG) printf("command: cp from %s to %s \n", src, dst);
    char *first_part = malloc(sizeof(char) * 1024);
    char *second_part = malloc(sizeof(char) * 1024);
    split_in_half(dst, &first_part, &second_part);
    int check_result;
    pthread_mutex_lock(&e_check_lock);
    // reason of the lock: please checkout ext2fsal_rm.c
    char *first = split(first_part);
    check_result = check_first_part(first, 2);  // 2 for root inode.
    pthread_mutex_unlock(&e_check_lock);
    if (DEBUG)
    {
        if (check_result & (1 << 16)){
            if(DEBUG) printf("valid path: %s, final inode = %d \n", dst, check_result & ~(1 << 16));
        }
        else {
            if(DEBUG) printf("invalid path: %s, final inode = %d \n", dst, check_result);
            free(first_part);
            free(second_part);
            return check_result;
        }
        if(DEBUG) printf("\n--------------------------\n");
    }

    /*
    for cp, if second part does not exist under its parent's dir, NICE, cp it.
       else if it's a file exists under its parent's dir, OKAY, overwrite it.
       else if it's a folder under it's parent's dir, create a file under this folder
            and name it with the first paramter of cp (real system file name). Of 
            course, if the file exists, overwrite it.
       else if the second part is empty, which means the path us in the form of 
            "/foo/bar/folder/", if nothing happened when checking the first part (valid), 
            create the file called <first parameter> under ../folder/  PERIOD. Of course,
            overwrite the existing file.
    */
    int parent_inode = check_result & ~(1 << 16);
    int writeto_inode;
    int new = 0;
    int contain_result = contains_file(parent_inode, second_part);
    struct ext2_inode *contain;
    if (contain_result != -1)
        contain = get_inode(contain_result);

    pthread_mutex_lock(&(inodes_lock_array[contain_result])); //ZZY
    if (strcmp(second_part, "") == 0 || contain_result == -1) {
        // allocate a new inode.
        if ((writeto_inode = find_free_inode()) == -1){
            if(DEBUG) printf("No free inode. return ENOSPC\n");
            pthread_mutex_unlock(&(inodes_lock_array[contain_result])); //ZZY
            free(first_part);
            free(second_part);
            return ENOSPC;
        }
        new = 1;
    }
    else if ((contain->i_mode & EXT2_S_IFLNK) == EXT2_S_IFLNK){
        // target file is a soft link, return EEXIST;
        if(DEBUG) printf("target file is a soft link, return EEXIST\n");
        pthread_mutex_unlock(&(inodes_lock_array[contain_result])); // ZZY
        free(first_part);
        free(second_part);
        return EEXIST;
    }
    else if (!(contain->i_mode & EXT2_S_IFREG)){
        // use existing inode.
        writeto_inode = contain_result;
    }
    else if (contain->i_mode & EXT2_S_IFDIR){
        pthread_mutex_unlock(&(inodes_lock_array[contain_result])); // ZZY
        parent_inode = contain_result;
        contain_result = contains_file(parent_inode, path);
        pthread_mutex_lock(&(inodes_lock_array[contain_result])); // ZZY
        if (contain_result == -1) {
            // allocate a new inode.
            if ((writeto_inode = find_free_inode()) == -1){
                if(DEBUG) printf("No free inode. return ENOSPC\n");
                pthread_mutex_unlock(&(inodes_lock_array[contain_result])); // ZZY
                free(first_part);
                free(second_part);
                return ENOSPC;
            }
            new = 1;
        }
        else if ((contain->i_mode & EXT2_S_IFLNK) == EXT2_S_IFLNK){
            // target file is a soft link, return EEXIST;
            if(DEBUG) printf("target file is a soft link, return EEXIST\n");
            pthread_mutex_unlock(&(inodes_lock_array[contain_result])); // ZZY
            free(first_part);
            free(second_part);
            return EEXIST;
        }
        else if (contain->i_mode & EXT2_S_IFREG){
            // use existing inode.
            writeto_inode = contain_result;
        }
        else if (contain->i_mode & EXT2_S_IFDIR){
            if(DEBUG) printf("cannot overwrite a dir\n");
            pthread_mutex_unlock(&(inodes_lock_array[contain_result])); // ZZY
            free(first_part);
            free(second_part);
            return EEXIST;
        }
    }
    else {
        if(DEBUG) printf("shouldn't get here: parent_inode = %d, contain = %d, second_part = %s\n",
            parent_inode, contain_result, second_part);
        pthread_mutex_unlock(&(inodes_lock_array[contain_result]));// ZZY usually not here
        free(first_part);
        free(second_part);
        exit(1);
    }
    pthread_mutex_unlock(&(inodes_lock_array[contain_result]));// ZZY

    // set inode data appropriately and allocate data blocks for the new file.
    pthread_mutex_lock(&(inodes_lock_array[writeto_inode])); // ZZY
    struct ext2_inode *real_writeto_inode = get_inode(writeto_inode);
    memset(real_writeto_inode, 0, sizeof(struct ext2_inode));
    if(DEBUG) printf("here1\n");
    if (new){ 
        real_writeto_inode->i_ctime = (unsigned int)time(NULL);
        real_writeto_inode->i_uid = 0;
        real_writeto_inode->i_mode |= EXT2_S_IFREG; 
        real_writeto_inode->i_links_count = 1;
        real_writeto_inode->i_blocks = 0;
        if(DEBUG) printf("TIME: creation time %d", (unsigned int)time(NULL));
    }
    
    else { // not new inode
        // bogdan said I cannot free existing blocks.
    }
    // char *write_filename = (strcmp(second_part, "") != 0) ? second_part : src;
    // char write_filename[EXT2_NAME_LEN + 1];
    

    if(DEBUG) printf("here2\n");
    int file_size = get_size(path);  
    int needed_block = CELL(file_size, EXT2_BLOCK_SIZE);
    if (needed_block > 12){
        if ((real_writeto_inode->i_blocks / 2) <= 12)  // if we didn't have indirected block.
            needed_block++;  // indirect block (i_block[12]) needs one more block.
    }


    // TODO: we need to make sure no thread is getting any block before we're done here.
    int already_have_block = 0;
    if (!new){
        already_have_block = real_writeto_inode->i_blocks / 2;
    }

    if (needed_block - already_have_block > sb->s_free_blocks_count){
        if(DEBUG) printf("don't have enough block left for file\n");
        if (new)
            set_free_inode(writeto_inode);
        pthread_mutex_unlock(&(inodes_lock_array[writeto_inode])); // ZZY
        free(first_part);
        free(second_part);
        return ENOSPC;
    }
    pthread_mutex_unlock(&(inodes_lock_array[writeto_inode])); // ZZY
    if(DEBUG) printf("here3\n");

    // update dir info
    pthread_mutex_lock(&(inodes_lock_array[parent_inode]));// ZZY
    struct ext2_inode *parent_i = get_inode(parent_inode);
    int target_block_num = parent_i->i_block[parent_i->i_blocks / 2 - 1];

    pthread_mutex_lock(&(block_lock_array[target_block_num]));// ZZY
    int curr_byte = 0;
    unsigned char *block = (unsigned char*)(disk + 1024 * target_block_num);
    struct ext2_dir_entry *dir;
    while(curr_byte != EXT2_BLOCK_SIZE){
        dir = (struct ext2_dir_entry *)(block + curr_byte);
        if (curr_byte + dir->rec_len == EXT2_BLOCK_SIZE){
            break;
        }
        curr_byte += dir->rec_len;
    }


    int capacity = dir->rec_len - UPFOUR(8 + dir->name_len); // left capacity of this block
    int need = strlen(second_part) + 8; // actucal need for the dir we need to add
    need = UPFOUR(need);
    int maybe_block;
    if (need > capacity)
    {
        maybe_block = find_free_block();
        if (maybe_block == -1)
        {
            if (new)
            {
                set_free_inode(writeto_inode); 
            }
            pthread_mutex_unlock(&(block_lock_array[target_block_num]));// ZZY
            pthread_mutex_unlock(&(inodes_lock_array[parent_inode]));// ZZY
            free(first_part);
            free(second_part);
            return ENOSPC;
        }
        else{
            pthread_mutex_lock(&(block_lock_array[maybe_block]));// ZZY
            struct ext2_dir_entry *new_block = (struct ext2_dir_entry *)(disk + 1024 * maybe_block);
            memset(new_block, 0, 1024);
            new_block->name_len = min(strlen(second_part), EXT2_NAME_LEN);
            new_block->rec_len = EXT2_BLOCK_SIZE;
            if (strcmp(second_part, "") != 0){
                if(DEBUG) printf("1.1\n");
                strncpy(new_block->name, second_part, EXT2_NAME_LEN);
            }
            else{
                if(DEBUG) printf("1.2\n");
                strncpy(new_block->name, path, EXT2_NAME_LEN);
            }
            new_block->inode = writeto_inode; 
            new_block->file_type = EXT2_FT_REG_FILE;
            pthread_mutex_unlock(&(block_lock_array[maybe_block]));// ZZY
            // update parent inode
            parent_i->i_blocks += 2;
            parent_i->i_block[parent_i->i_blocks / 2 - 1] = maybe_block;
            parent_i->i_size = parent_i->i_blocks / 2 * EXT2_BLOCK_SIZE;
            pthread_mutex_unlock(&(block_lock_array[target_block_num]));// ZZY
            pthread_mutex_unlock(&(inodes_lock_array[parent_inode]));// ZZY
        }
    }
    else{
        maybe_block = -1;
        dir->rec_len = UPFOUR(dir->name_len + 8); // update the rec_len
        struct ext2_dir_entry *new_dir = (void *)dir + dir->rec_len;
        new_dir->file_type = EXT2_FT_REG_FILE;
        new_dir->inode = writeto_inode; 
        if(DEBUG) printf("what the fuck is going on here %d\n", strcmp(second_part, ""));
        if(DEBUG) printf("what is second_part %s\n", second_part);
        if(DEBUG) printf("what is src %s\n", path);
        if (strcmp(second_part, "") != 0){
            if(DEBUG) printf("2.1\n");
            strncpy(new_dir->name, second_part, EXT2_NAME_LEN);
        }
        else{
            if(DEBUG) printf("2.2\n");
            strncpy(new_dir->name, path, EXT2_NAME_LEN);
        }
        new_dir->name_len = min(strlen(new_dir->name), EXT2_NAME_LEN);
        new_dir->rec_len = capacity;
        pthread_mutex_unlock(&(block_lock_array[target_block_num]));// ZZY
        pthread_mutex_unlock(&(inodes_lock_array[parent_inode]));// ZZY
    }
    // we may already have some blocks, so don't alloc blocks for those entries.
    
    if(DEBUG) printf("new: %d\n", new);
    if(DEBUG) printf("here4\n");
    int new_block_count = 0;
    int indirect = 0;
    pthread_mutex_lock(&(inodes_lock_array[writeto_inode])); // ZZY
    if ((real_writeto_inode->i_blocks / 2) > 12){  // we already have a indirect block
        indirect = real_writeto_inode->i_block[12];
    }
    int i;
    for (i = 0; i < needed_block; i++){
        if (i <= 11){
            if (!(i < already_have_block)){
                real_writeto_inode->i_block[i] = find_free_block(); 
                new_block_count++;
            }
        }
        else{
            if (!indirect){  // we dont have indirect block yet. alloc one.
               real_writeto_inode->i_block[12] = find_free_block(); 
               indirect = real_writeto_inode->i_block[12];
            }
            // we have an indirect block.
            if (!(i < already_have_block)){ 
                int *indirect_array = (int *)(disk + (indirect) * 1024);
                indirect_array[i - 12] = find_free_block(); 
                new_block_count++;
            }
        }
    }
    
    
    if(DEBUG) printf("here5\n");
    // update metadata
    if (new_block_count > 0){
        if(DEBUG) printf("in if\n");
        real_writeto_inode->i_blocks = needed_block * 2;
    }
    else if (!new && real_writeto_inode->i_blocks / 2 > needed_block){
        if(DEBUG) printf("in else if\n");
        // we need to free exceed blocks.
        int have_indirect = 0;
        for (i = needed_block; i < real_writeto_inode->i_blocks / 2; i++){
            if (needed_block <= 11){
                set_free_block(real_writeto_inode->i_block[i]);
            }
            else{
                if (!have_indirect){
                    have_indirect = real_writeto_inode->i_block[12];
                }
                int *indirect_array = (int *)(disk + (have_indirect) * 1024);
                set_free_block(indirect_array[i - 12]);
            }
        }
        if (real_writeto_inode->i_blocks / 2 > 12 || needed_block <= 12)
            set_free_block(have_indirect);
        real_writeto_inode->i_blocks = needed_block * 2;
    }
    

    if(DEBUG) printf("here6\n");
    // at this point we have all the resources to write to inode.
    // write to data blocks
    FILE *fp = fopen(src, "r");
    if (fp == NULL){
        if(DEBUG) printf("src file doesn't exist\n");
        free(first_part);
        free(second_part);
        return ENOENT;
    }
    if(DEBUG) printf("here7\n");
    for (i = 0; i < needed_block; i++){
        if (fseek(fp, i * EXT2_BLOCK_SIZE, SEEK_SET) == -1){
            perror("fseek\n");
            exit(1);
        }
        // read to the block
        char *block;
        if (i <= 11){
            block = (char *)(disk + 1024 * (real_writeto_inode->i_block[i]));
        }
        else {
            int *indirect_array = (int *)(disk + (real_writeto_inode->i_block[12]) * 1024);
            block = (char *)(disk + 1024 * (indirect_array[i - 12]));
        }
        memset(block, 0, EXT2_BLOCK_SIZE);
        int byte_read = fread(block, 1, EXT2_BLOCK_SIZE, fp);
        if(DEBUG) printf("this time, read [%d] bytes from file\n", byte_read);
        
    }
    if(DEBUG) printf("here8\n");
    real_writeto_inode->i_size = file_size;
    pthread_mutex_unlock(&(inodes_lock_array[writeto_inode])); // ZZY
    if(DEBUG) printf("file_size = %d\n", file_size);

    free(first_part);
    free(second_part);
    return 0;
}