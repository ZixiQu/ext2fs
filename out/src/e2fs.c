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

/**
 * TODO: Make sure to add all necessary includes here
 */

#include "e2fs.h"
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>


extern char *disk;
extern unsigned char *db_bm;
extern unsigned char *in_bm;

 /**
  * TODO: Add any helper implementations here
  */

/* given a string. return a strtok form of char pointer. 

The return value should be used like this:
char string[] = "/foo/bar//split/";
char *piece = split(string);   <-- 
while(piece){
    printf("%s \n");  // the space between %s and \n is IMPORTANT
    piece = strtok(NULL, "/");
}

*/
char *split(const char *string){
    // using a copy of string so that it doesn't change it.
    // You can't change it. This task passed in a const char *
    char *buffer = malloc(sizeof(char) * 1024);  
    strcpy(buffer, string);

    return strtok(buffer, "/");
}

/* return the last char of the string. return NULL(0) if string is empty*/
char find_last_char(const char *string){
    int length = strlen(string);
    if (length == 0){
        return 0;
    }
    return string[length - 1];
}

/* 
check if data block <index> had been occupied 
return non-zero if block had been occupied, return 0 on not. 
*/
int db_occupied(int index) {
    int real_index = index - 1;
    return db_bm[real_index / 8] & (1 << (real_index % 8));
}

/* 
check if inode <index> had been occupied 
return non-zero if block had been occupied, return 0 on not. 
*/
int in_occupied(int index) {
    int real_index = index - 1;
    return in_bm[real_index / 8] & (1 << (real_index % 8));
}

/* split_in_half(): 
break the path into two parts: everything before the first slash, and after.

example:
path1 = "/foo/bar/file";  
break into "/foo/bar/" and "file"

path2 = "/foo/bar/folder/"
for path2, break into "/foo/bar/folder/" and ""

*/
void split_in_half(const char *path, char **first, char **second) {
    int index = strlen(path) - 1;
    while(index){
        if (path[index] == '/'){
            break;
        }
        index--;
    }
    // at this point index == the first occurence of / on the right
    strncpy(*first, path, index);
    (*first)[index] = '\0';
    strncpy(*second, path + index + 1, strlen(path) - index - 1);
}

/* get the struct inode of <index> */
struct ext2_inode *get_inode(int index){
    if (index < 1){
        if(DEBUG) printf("get_inode fault\n");
        exit(1);
    }
    struct ext2_inode *inodes = (struct ext2_inode *)(disk + 1024 * 5);
    return &(inodes[index - 1]);
}

/* 
@para:
first: folder name,
inode: inode number of parent folder.

1) for the first part, recursively check if folder <foldername> exists under 
it's parent path, return ENOENT if it doesn't succeed in any depth.
This recursion function should return the last entry of the last folder in path. 

return errno if anything goes wrong
*return inode number | (1 << 16) on succeed.
reason on doing this:
    I need to know whether the return value is ERRNO or inode number.
    There were a few approach: 
    a) return inode and ERRNO itself:
        downside: don't know if return value is inode or ERRNO

    b) On succeed return 0 and set a global variable to inode number .
        downside: Concurrency problem

    c) (in use) if returing an inode return inode | (1 << 16) to distinguish return value.

/foo/bar/folder/

*/
int check_first_part(char *first, int inode){
    // base case: if cannot found, return errno. 
    // Or first is empty(hit lowest layer) return 0.
    if(DEBUG) printf("at the beginning of recursion checking [%s]\n", first);
    if (!first){
        return inode | (1 << 16);
    }
    if(DEBUG) printf("*** inode: [%d] \n", inode);
    assert(in_occupied(inode));
    struct ext2_inode *curr = get_inode(inode);
    
    // reads from all direct blocks
    int found = 0;
    int found_inode;
    int i;
    if(DEBUG) printf("curr iblocks%d\n", curr->i_blocks);
    for (i = 0; i < min((curr->i_blocks) / 2, 12); i++){
        if(DEBUG) printf("under for\n");
        int curr_byte = 0;
        unsigned char *block = (unsigned char*)(disk + 1024 * (curr->i_block)[i]);
        while(curr_byte != EXT2_BLOCK_SIZE){
            if(DEBUG) printf("under while \n");
            struct ext2_dir_entry *dir = (struct ext2_dir_entry *)(block + curr_byte);
            if(DEBUG) printf("checking [%s] and [%s] \n", first, dir->name);
            if (strncmp(first, dir->name, dir->name_len) == 0 && dir->file_type == EXT2_FT_DIR){
                // found a *dir* that is the name we want.
                found_inode = dir->inode;
                found = 1;
                break;
            }
            
            curr_byte += dir->rec_len;
        }
        
        if (found) break;
    }
    // no need to search in indirect block for file.

    if (!found){
        // in this case the dir name <first> cannot be found under it's parent.
        if(DEBUG) printf("cannot found file [%s] under inode[%d]\n", first, inode);
        return ENOENT;
    }

    // inductive case: recursion on the new folder
    return check_first_part(strtok(NULL, "/"), found_inode);
}

/* check if <filename> exists under dir <inode>. *inode is an int: 2 for root
return -1 if not found, otherwise returns inode number of the file.
*/
int contains_file(int inode, char *filename) {
    if(DEBUG) printf("checking if inode [%d] is occupied: %d \n", inode, in_occupied(inode));
    assert(in_occupied(inode));
    pthread_mutex_lock(&(inodes_lock_array[inode]));
    struct ext2_inode *curr = get_inode(inode);

    int i;
    if(DEBUG) printf("curr iblocks%d\n", curr->i_blocks);
    for (i = 0; i < min((curr->i_blocks) / 2, 12); i++){
        if(DEBUG) printf("under for\n");
        int curr_byte = 0;
        pthread_mutex_lock(&(block_lock_array[i]));
        unsigned char *block = (unsigned char*)(disk + 1024 * (curr->i_block)[i]);
        while(curr_byte != EXT2_BLOCK_SIZE){
            if(DEBUG) printf("under while \n");
            struct ext2_dir_entry *dir = (struct ext2_dir_entry *)(block + curr_byte);
            if(DEBUG) printf("checking [%s] and [%s] \n", filename, dir->name);
            if (strncmp(filename, dir->name, max(dir->name_len, strlen(filename))) == 0 && dir->inode != 0){
                pthread_mutex_unlock(&(block_lock_array[i]));
                pthread_mutex_unlock(&(inodes_lock_array[inode]));
                return dir->inode;
            }
            curr_byte += dir->rec_len;
        }
        pthread_mutex_unlock(&(block_lock_array[i]));
    }
    // from tips, I do not need to find file in indirect block.
    pthread_mutex_unlock(&(inodes_lock_array[inode]));
    return -1;   // didn't find the file.
}

// find free inode return -1 if no free number else return inode number
int find_free_inode(){
    pthread_mutex_lock(&i_bitmap_lock);
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            int mask = 1 << j;
            if (!(in_bm[i] & mask)){
                in_bm[i] |= mask;

                pthread_mutex_lock(&sb_lock);
                sb->s_free_inodes_count--;
                pthread_mutex_unlock(&sb_lock);

                pthread_mutex_lock(&gd_lock);
                gd->bg_free_inodes_count--;
                pthread_mutex_unlock(&gd_lock);

                pthread_mutex_unlock(&i_bitmap_lock);
                return i * 8 + j + 1; 
            }
        }
    }
    pthread_mutex_unlock(&i_bitmap_lock);
    return -1;   
}

void set_free_inode(int index){
    index = index - 1;
    pthread_mutex_lock(&i_bitmap_lock);
    int mask = 1 << (index % 8);
    in_bm[index/8] &= ~(mask);

    pthread_mutex_lock(&sb_lock);
    sb->s_free_inodes_count++;
    pthread_mutex_unlock(&sb_lock);

    pthread_mutex_lock(&gd_lock);
    gd->bg_free_inodes_count++;
    pthread_mutex_unlock(&gd_lock);

    pthread_mutex_unlock(&i_bitmap_lock);
}


// return -1 if no free block number else return block number
int find_free_block(){
    pthread_mutex_lock(&d_bitmap_lock);
    for (int i = 0; i < 128/8 ; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            int mask = 1 << j;
            if (!(db_bm[i] & mask))
            {
                db_bm[i] |= mask;

                pthread_mutex_lock(&sb_lock);
                sb->s_free_blocks_count--;
                pthread_mutex_unlock(&sb_lock);

                pthread_mutex_lock(&gd_lock);
                gd->bg_free_blocks_count--;
                pthread_mutex_unlock(&gd_lock);

                pthread_mutex_unlock(&d_bitmap_lock);
                return i * 8 + j + 1;
            }
        }
    }
    pthread_mutex_unlock(&d_bitmap_lock);
    return -1;
}

void set_free_block(int index){
    index = index - 1;
    pthread_mutex_lock(&d_bitmap_lock);
    int mask = 1 << (index % 8);
    db_bm[index/8] &= ~(mask);

    pthread_mutex_lock(&sb_lock);
    sb->s_free_blocks_count++;
    pthread_mutex_unlock(&sb_lock);

    pthread_mutex_lock(&gd_lock);
    gd->bg_free_blocks_count++;
    pthread_mutex_unlock(&gd_lock);

    pthread_mutex_unlock(&d_bitmap_lock);

}

int get_size(char *filename){
    FILE * fd = fopen(filename, "r");
    if (!fd)
    {
        return -1; // open fail  should not here
    }
    fseek(fd, 0, SEEK_END); //point from head to tail
    int size = ftell(fd); // get current point in fd
    fclose(fd);
    return size;
}