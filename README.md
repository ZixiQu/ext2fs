# ext2fs
A simulation of exts file system. In this project, cp mkdir rm hl(hardlink) sl(softlink) operations, as well as the main frame, is implemented.



credit: [Zhenyu Zhao](https://github.com/andyzhaozhenyu) for co-design and coding with me



## Design approaches

#### locks and their functionality:

```C
pthread_mutex_t e_check_lock;           // error checking lock. Refers to general section below.
pthread_mutex_t sb_lock;                // super block lock.
pthread_mutex_t gd_lock;                // group descriptor lock
pthread_mutex_t i_bitmap_lock;          // inode bitmap lock
pthread_mutex_t d_bitmap_lock;          // data block bitmap lock
pthread_mutex_t block_lock_array[128];  // array of lockes. One for each block
pthread_mutex_t inodes_lock_array[32];  // array of lockes. One for each inode.
```

This design ensures the efficiency by: 

- not having only one lock to lock at the beginning of the operation and unlock at the end of the operation. Cheat in this way to unsure concurrent correctness will make operations sequential, not sychronized.

- having a seperate lock for each inode and each block so that multiple processes can access different inodes and blocks at the same time.

- The fundamental idea is, add a lock when reading from or writing to a important piece: inode, block, metadata...

#### general:

need to add a lock when error checking. We used strtok() from string library to split the path. This function stores the first given string in a global, which if another thread called strtok() on a new string will change the global, so this operation must be mutual excluded.

#### e2fs.c:

- find_free_*() and set_free_*(): lock the bitmap when finding free inodes and blocks. Lock sb and gd when updating the metadata.

- contains_file(): reading data from inode, lock it. Reading data from some blocks, lock them. Unlock the inode and block locks before returning.

- no need to lock check_first_part(): there is a lock for strtok(), and this always comes along with check_first_part(), so no more locks needed.

- no need to lock get_inode(): in practice, we need to operate on the inode struct we got from this function, so lock it carefully when we call this function rather then embeded in the function.

#### rm:

   - read from parent_inode to find the last dir entry, lock the inode in outer layer
   - iterating through all blocks of the dir, lock each blocks appropriately. 
   - Add the new dir entry, aka update the last block of parent dir. Lock it when do so.
   - Assure all locks are set free before ANY returns.

#### mkdir:

- First three parts are the same with rm.

- Focus more on the third part, sometimes we have one more block to maintain (maybe_block, we have this extra block when we need a new block to store dir_entry). Lock this block when using it.
- initialize the new inode, lock it.

- Putting "." and ".." into the new directory, aka writing to the new block for new dir, lock this block. 

#### cp:

- First three parts are the same with rm.
- Unlock every lock berfore return
- More specifically, we calculated and determined whether the number of the left free blocks is enough for the new coming file, before we correctly locked up the blocks for the new file, no other thread can grab the ownership of any blocks. 

#### hl:

- for both src and dst, read from parent_inode to find the last dir entry, lock the inode in outer layer.
- lock the blocks in the inner layer.

#### sl:

   - similar with hl
   - but we have one more inode here, and we need to lock corresponding resouce as we did in cp and mkdir.
