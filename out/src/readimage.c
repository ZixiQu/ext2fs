#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"

unsigned char *disk;

void print_bitmap(unsigned char *bm, int size){
    int i, j;
    for (int i = 0; i < size; ++i){ 
        for (int j = 0; j < 8; ++j){
            char mask = 1 << j;
            unsigned char result = bm[i] & mask;
            if (result) printf("1");
            else printf("0");
        }
        printf(" ");
    }
    printf("\n");
}

int occupied(unsigned char *bm, int bit){
    unsigned char mask = 1 << (bit % 8);
    return bm[bit / 8] & mask;
}

void print_one_node(struct ext2_inode *inodes, int index){
    if (index > 32 || index < 1) printf("ERROR bad inode index>: %d\n", index);
    else{
        char mode;
        int i = index - 1;
        if (inodes[i].i_mode & EXT2_S_IFREG)
        {
            mode = 'f';
        }
        else if (inodes[i].i_mode & EXT2_S_IFDIR)
        {
            mode = 'd';
        }
        else mode = '0';
        printf("[%d] type: %c size: %d links: %d blocks: %d \n", 
        i+1, mode, inodes[i].i_size,
        inodes[i].i_links_count, inodes[i].i_blocks);
        printf("[%d] Blocks:", i + 1);
        int j;
        for (j = 0; j < inodes[i].i_blocks / 2; ++j)
            {
            printf(" %d", inodes[i].i_block[j]);
            }
        printf("\n");
    }
}

void print_inodes(struct ext2_inode *inodes, unsigned char *bm){
    int i;
    print_one_node(inodes, 2);
    for (i = 12; i < 32; ++i)
    {
        if (occupied(bm, i-1)) {
            print_one_node(inodes, i);
        }
    }
};
void print_dir_block(int bnum){
    int byte_index = 0;
    unsigned char *block = (unsigned char *)(disk + 1024 * (bnum));

    while(byte_index != EXT2_BLOCK_SIZE){
        struct ext2_dir_entry *dir = (struct ext2_dir_entry *)(block + byte_index);
        int name_len = (int) dir->name_len;
        char type = '0';

        if (dir->file_type == 2)
        {
            type = 'd';
        }
        else if (dir->file_type == 1)
        {
            type = 'f'; 
        }
    
        printf("Inode: %d rec_len: %d name_len: %d type = %c name=", dir->inode,
            dir->rec_len, name_len, type);

        char *name = (char *)(block + byte_index + sizeof(struct ext2_dir_entry));
        int j;
        for (j = 0; j < name_len; ++j)
        {
            printf("%c", name[j]);
        }
        printf("\n");
        byte_index += dir->rec_len;
    }
}

void print_blocks(struct ext2_inode *inodes,
    unsigned char *inode_bm, unsigned char *block_bm){
    int i, j;
    for (j = 0; j < inodes[1].i_blocks/2; j++ )
    {
        printf("    DIR BLOCK NUM: %d (for inode %d)\n", inodes[1].i_block[j], 2);
        print_dir_block(inodes[1].i_block[j]);
    }
    for (i = 12; i < 32; ++i)
    {
        if (occupied(inode_bm, i-1))
        {
            if (EXT2_S_IFDIR & inodes[i-1].i_mode)
            {
                for (j = 0; j < inodes[i-1].i_blocks/2; ++j)
                {
                    printf("    DIR BLOCK NUM: %d (for inode %d)\n", inodes[i-1].i_block[j], i);
                    print_dir_block(inodes[i-1].i_block[j]);
                }
            }        
        }
    }
}
int main(int argc, char **argv) {

    if(argc != 2) {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
    printf("Inodes: %d\n", sb->s_inodes_count);
    printf("Blocks: %d\n", sb->s_blocks_count);

    struct ext2_group_desc * gd = (struct ext2_group_desc *)(disk + 2048);
    printf("Block group:\n");
    printf("    block bitmap: %d\n", gd->bg_block_bitmap);
    printf("    inode bitmap: %d\n", gd->bg_inode_bitmap);
    printf("    inode table: %d\n", gd->bg_inode_table);
    printf("    free blocks: %d\n", gd->bg_free_blocks_count);
    printf("    free inodes: %d\n", gd->bg_free_inodes_count);
    printf("    used_dirs: %d\n", gd->bg_used_dirs_count);


    unsigned char *block_bits = (unsigned char*)(disk + 1024 * 3);
    printf("Block bitmap: ");
    print_bitmap(block_bits, sb->s_blocks_count/8);

    unsigned char *inode_bits = (unsigned char*)(disk + 1024 * 4);
    printf("Inode bitmap: ");
    print_bitmap(inode_bits, sb->s_inodes_count/8);

    struct ext2_inode *inodes = (struct ext2_inode *)(disk + 1024 * 5);

    printf("\nInodes: \n");
    print_inodes(inodes, inode_bits);

    printf("\n Directory Blocks: \n");
    print_blocks(inodes, inode_bits, block_bits);

    


    return 0;
}
