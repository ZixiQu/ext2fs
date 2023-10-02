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

#define min(x, y) ((x >= y) ? y: x)  // just like python min().

extern char *disk;
extern unsigned char *db_bm;
extern unsigned char *in_bm;



int32_t ext2_fsal_rm(const char *path)
{
    /**
     * TODO: implement the ext2_rm command here ...
     * the argument 'path' is the path to the file to be removed.
     */

    if (DEBUG) printf("command: rm %s \n", path);
    char *first_part = malloc(sizeof(char) * 1024);
    char *second_part = malloc(sizeof(char) * 1024);
    split_in_half(path, &first_part, &second_part);
    int check_result;
    pthread_mutex_lock(&e_check_lock);
    /* The reason for adding a lock here:
    split() function calls strtok() from string.h, which saves the string in global 
    on first call.
    If any other thread calls strtok() on a new string, the global will be changed and 
    causes concurrency problem. So the call of strtok() must be mutual excluded.

    I know it is awful, but we had rather short time to implement a file system, so we 
    decided not to implement a python split() on our own that takes hours.

    Since the code is highly modularized, we only need a new split() function to fix this.
    */
    char *first = split(first_part);
    check_result = check_first_part(first, 2);  // 2 for root inode.
    pthread_mutex_unlock(&e_check_lock);
    
    if (check_result & (1 << 16)){
        if (DEBUG) printf("valid path: %s, final inode = %d \n", path, check_result & ~(1 << 16));
    }
    else {
        if (DEBUG) printf("invalid path: %s, final inode = %d \n", path, check_result);
        free(first_part);
        free(second_part);
        return check_result;
    }
    if (DEBUG) printf("\n--------------------------\n");
    /*
    for rm, if the second part is a file under its parent's dir, NICE, remove it...
       else if it's a folder under it's parent's dir, return EISDIR 
       else if the second part is empty, that means the path is in the form of:
            "/foo/bar/folder/" (with a trailing /), and the last part is a dir, 
            raise EISDIR
       else if the second part doesn't exist under it's parent's dir, raise ENOENT.
    */
    int return_value;
    int parent_inode = check_result & ~(1 << 16);
    int target_inode;
    if (DEBUG) printf("parent_inode: [%d]\n", parent_inode);
    if (strcmp(second_part, "") == 0){  // The third case: if second part is empty string.
        // explaination above
        if (DEBUG) printf("second_part is empty. return EISDIR\n");
        return_value = EISDIR;
    }

    else if ((target_inode = contains_file(parent_inode, second_part)) != -1) {
        if (DEBUG) printf("here we got it!\n");
        // add inode lock ZZY
        pthread_mutex_lock(&(inodes_lock_array[target_inode]));
        struct ext2_inode *freed_inode = get_inode(target_inode);
        if (freed_inode->i_mode & EXT2_S_IFDIR) {
            if (DEBUG) printf("target is a dir. return EISDIR \n");
            // add inode unlock ZZY
            pthread_mutex_unlock(&(inodes_lock_array[target_inode]));
            return_value = EISDIR;
        }
        else {
            /*
            deleting an inode: decrement the links count. if links count drop
             to 0 then actually delete(free) the corresponding inode and update metadata.
            */

            (freed_inode->i_links_count)--;
            pthread_mutex_unlock(&(inodes_lock_array[target_inode])); // ZZY
            int found = 0;
            int i;
            struct ext2_dir_entry *dir = NULL;
            struct ext2_dir_entry *prev;
            unsigned int block_num;           //  ZZY
            // maybe deadlock here
            pthread_mutex_lock(&(inodes_lock_array[parent_inode]));
            struct ext2_inode *parent_i = get_inode(parent_inode);
            for (i = 0; i < min((parent_i->i_blocks) / 2, 12); i++){
                if (DEBUG) printf("under for\n");
                int curr_byte = 0;
                prev = NULL;  // changed on 12.7
                // add lock 
                block_num = (parent_i->i_block)[i]; // ZZY 
                pthread_mutex_lock(&(block_lock_array[block_num])); // ZZY

                unsigned char *block = (unsigned char*)(disk + 1024 * (parent_i->i_block)[i]);
                while(curr_byte != EXT2_BLOCK_SIZE){
                    if (DEBUG) printf("under while \n");
                    dir = (struct ext2_dir_entry *)(block + curr_byte);
                    if (DEBUG) printf("checking [%s] and [%s] \n", first, dir->name);
                    if (strncmp(second_part, dir->name, dir->name_len) == 0){
                        // found a *dir* that is the name we want.
                        found = 1;
                        break;
                    }
                    else{
                        prev = (struct ext2_dir_entry *)(block + curr_byte);
                    }
                    curr_byte += dir->rec_len;
                }
                pthread_mutex_unlock(&(block_lock_array[block_num]));
                if (found){  // added by Zixi Qu
                    break;
                }
            }
            pthread_mutex_unlock(&(inodes_lock_array[parent_inode]));

            pthread_mutex_lock(&block_lock_array[block_num]);
            // update the last dir's rec_len
            if (prev)
                prev->rec_len += dir->rec_len;  // delete element that is not the first.
            else {
                dir->inode = 0;  // dir is a first element of the block. set inode to 0
            }
            // add unlock here ZZY
            pthread_mutex_unlock(&block_lock_array[block_num]); 

            // add inode lock here ZZY 
            pthread_mutex_lock(&(inodes_lock_array[target_inode]));
            if (freed_inode->i_links_count == 0){
                // update last rec_len to cover curr dir_entry.
                
                freed_inode->i_dtime = (unsigned int)time(NULL);

                // update metadata.
                set_free_inode(target_inode);
                int indirect = 0;
                for (i = 0; i < freed_inode->i_blocks / 2; i++){
                    if (i <= 11){
                        set_free_block(freed_inode->i_block[i]);
                    }
                    else{
                        if (!indirect){
                            indirect = freed_inode->i_block[12];
                        }
                        int *indirect_array = (int *)(disk + indirect * 1024);
                        set_free_block(indirect_array[i - 12]);
                    }
                }
            }
            // ZZY
            pthread_mutex_unlock(&(inodes_lock_array[target_inode]));
        }
        return_value = 0;
        
    }
    else {
        if (DEBUG) printf("cannot find file [%s] under folder inode [%d]. return ENOENT\n", 
            second_part, parent_inode);
        return_value = ENOENT;
    }

    free(first_part);
    free(second_part);
    if (DEBUG) printf("returned : [%d]\n", return_value);
    return return_value;
}