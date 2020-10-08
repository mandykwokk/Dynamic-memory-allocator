/**
 * All functions you make for the assignment must be implemented in this file.
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stdint.h>
#include "debug.h"
#include "sfmm.h"

void *sf_malloc(size_t size) {
    if(size==0) //if 0 return NULL without errno
        return NULL;
    size_t pad = (16-((size+16)%16));
    if(pad==16)
        pad = 0;
    size_t required = (size+16)+pad; //required block size calculated
    if(sf_mem_start()==sf_mem_end()){// first malloc
        if(sf_mem_grow()==NULL){
            sf_errno = ENOMEM;
            return NULL;
        }//initialize free list, initial prologue
        for(int i=0;i<NUM_FREE_LISTS;i++){
            sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
            sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
        }
        sf_prologue* prologue_ptr = sf_mem_start();
        sf_header prologue_header = 32|PREV_BLOCK_ALLOCATED|THIS_BLOCK_ALLOCATED;
        sf_footer prologue_footer = (32|PREV_BLOCK_ALLOCATED|THIS_BLOCK_ALLOCATED)^sf_magic();
        prologue_ptr -> header = prologue_header;
        prologue_ptr -> footer = prologue_footer;//initial epilogue
        sf_epilogue* epilogue_ptr = (void*)sf_mem_start()+PAGE_SZ-sizeof(*epilogue_ptr);
        sf_header epilogue_header = 0|THIS_BLOCK_ALLOCATED; //prv alloc
        epilogue_ptr -> header = epilogue_header;//4096-48(prologue40+epilogue8) free block
        struct sf_block* initial_free = (struct sf_block*)&(prologue_ptr->footer);
          sf_header initial_free_header = (PAGE_SZ-sizeof(*prologue_ptr)-sizeof(*epilogue_ptr))|PREV_BLOCK_ALLOCATED;
        initial_free -> header = initial_free_header;
        initial_free -> body.links.next = (struct sf_block *)initial_free;//&(initial_free->header);
        initial_free -> body.links.prev = (struct sf_block *)initial_free;//&(initial_free->header);
        sf_block* initial_next_block = (void*)initial_free+(initial_free->header&BLOCK_SIZE_MASK);
        sf_footer initial_free_footer= initial_free_header^sf_magic();
        initial_next_block->prev_footer = initial_free_footer;//finish initial free block
        //init_free_list();
        insert_to_freelist(initial_free);
        //
    }//search for free block, allocate/split
    int minIndex = getIndex((size_t)required);
    //calculated the index of fit block
    sf_block* fit_block = NULL;
    if(minIndex<NUM_FREE_LISTS){//take the head of array[index]//block found
        fit_block = search_for_fit(minIndex,required);
        if(fit_block!=NULL)
            remove_from_free(fit_block);
    }
    while(fit_block==NULL){//extend heap
        sf_epilogue* old_epilogue = (sf_epilogue*)sf_mem_end()-1;
        sf_epilogue* block_start = (sf_epilogue*)sf_mem_end()-2;
        if(sf_mem_grow()==NULL){
            sf_errno = ENOMEM;
            return NULL;
        }//write new epilogue
        sf_epilogue* new_epilogue = (sf_epilogue*)sf_mem_end()-1;
        sf_footer* last_footer = (void*)new_epilogue-sizeof(last_footer);
        //10.25
        int epilogue_prev_alloc = ((*last_footer^sf_magic())&THIS_BLOCK_ALLOCATED);
        if(epilogue_prev_alloc==0)
            new_epilogue->header = old_epilogue->header;
        else
            new_epilogue->header = old_epilogue->header|PREV_BLOCK_ALLOCATED;
        sf_block* newBlock = (void*)block_start;
        newBlock->header = (PAGE_SZ)|(new_epilogue->header&PREV_BLOCK_ALLOCATED);
        newBlock->body.links.prev = newBlock;
        newBlock->body.links.next = newBlock;
        sf_block* footerBlock = (void*)&(newBlock->prev_footer)+((newBlock->header)&BLOCK_SIZE_MASK);
        footerBlock->prev_footer = (newBlock->header)^sf_magic();
        coalesce(newBlock);//with prev footer
        int minIndex = getIndex((size_t)required);
        if(minIndex<NUM_FREE_LISTS){//take the head of array[index]//block found
            fit_block = search_for_fit(minIndex, required);
            if(fit_block!=NULL)
                remove_from_free(fit_block);
        }
    }//fit block found
    int fit_block_size = fit_block->header&BLOCK_SIZE_MASK;
    int remain_size = fit_block_size-required;
    if(remain_size>32){
        sf_block* allocate = fit_block;
        allocate->header = required|THIS_BLOCK_ALLOCATED|(fit_block->header&PREV_BLOCK_ALLOCATED);
        sf_footer* allocate_footer = (void*)allocate+required;
        *allocate_footer = allocate->header^sf_magic();
        //new free block after split
        sf_block* split_block = (void*)allocate_footer;
        split_block->header = remain_size|PREV_BLOCK_ALLOCATED;
        split_block->body.links.next = split_block;
        split_block->body.links.prev = split_block;
        sf_footer* split_block_footer = (void*)split_block+remain_size;
        *split_block_footer = split_block->header^sf_magic();//next block prev alloc to 0
        insert_to_freelist(split_block);
        updateEpilogue();
        return (void*)&allocate->body.payload;
    }
    else{
        int prev_alloc = (fit_block->prev_footer^sf_magic())&THIS_BLOCK_ALLOCATED;
        if(prev_alloc==0)
            fit_block->header = fit_block_size|THIS_BLOCK_ALLOCATED;
        else
            fit_block->header = fit_block_size|THIS_BLOCK_ALLOCATED|PREV_BLOCK_ALLOCATED;
        sf_footer* fit_block_footer = (void*)&fit_block->prev_footer+fit_block_size;
        *fit_block_footer = fit_block->header^sf_magic(); //next block prev alloc to 1
        //just added
        sf_block* next_block = (void*)fit_block+(fit_block->header&BLOCK_SIZE_MASK);
        if((next_block->header&BLOCK_SIZE_MASK)!=0&&((uintptr_t)(void*)next_block+16)!=(uintptr_t)sf_mem_end()){
            next_block->header|=0x1;
            sf_footer* next_block_footer = (void*)next_block+(next_block->header&BLOCK_SIZE_MASK);
            *next_block_footer = next_block->header^sf_magic();
        }

        updateEpilogue();
        return (void*)&fit_block->body.payload;
    }
    //return ptr;
}

void updateEpilogue(){
    sf_epilogue* new_epilogue = (sf_epilogue*)sf_mem_end()-1;
    sf_footer* last_footer = (void*)((sf_epilogue*)sf_mem_end()-2);
    int epilogue_prev_alloc = ((*last_footer)^sf_magic())&THIS_BLOCK_ALLOCATED;
    if(epilogue_prev_alloc!=0)
        new_epilogue->header |= PREV_BLOCK_ALLOCATED;
}

void coalesce(sf_block* middleBlock){//include prev_footer
    int prev_alloc = (middleBlock->prev_footer^sf_magic())&THIS_BLOCK_ALLOCATED;
    sf_header* next_header = (void*)&(middleBlock->header)+((middleBlock->header)&BLOCK_SIZE_MASK);
    int next_alloc = (*next_header)&THIS_BLOCK_ALLOCATED;//printf("prev %d next %d\n", prev_alloc,next_alloc);
    sf_block* upper_addr = middleBlock;
    if(prev_alloc==0){
        int prev_block_size = (middleBlock->prev_footer^sf_magic())&BLOCK_SIZE_MASK;
        sf_block* prev_block_ptr = (void*)&(middleBlock->prev_footer)-prev_block_size;//include prev_footer
        upper_addr = prev_block_ptr;
        int mid_block_size = (middleBlock->header)&BLOCK_SIZE_MASK;
        int new_block_size = prev_block_size+mid_block_size;
        remove_from_free(prev_block_ptr);
        prev_block_ptr->header = new_block_size|(prev_block_ptr->header&PREV_BLOCK_ALLOCATED);
        prev_block_ptr->body.links.prev = prev_block_ptr;
        prev_block_ptr->body.links.next = prev_block_ptr;
        sf_footer* new_footer = (void*)middleBlock+mid_block_size;
        *new_footer = (prev_block_ptr->header)^sf_magic();
    }
    if(next_alloc==0){//&&(next_header!=(void*)sf_mem_end()-8)){//sf_block* upper_addr
        sf_block* next_block_ptr = (void*)&(middleBlock->prev_footer)+((middleBlock->header)&BLOCK_SIZE_MASK);
        int upper_block_size = (upper_addr->header)&BLOCK_SIZE_MASK;
        int next_block_size = next_block_ptr->header&BLOCK_SIZE_MASK;
        int new_block_size = upper_block_size+next_block_size;
        remove_from_free(next_block_ptr);
        upper_addr->header = new_block_size|(upper_addr->header&PREV_BLOCK_ALLOCATED);
        upper_addr->body.links.prev = upper_addr;
        upper_addr->body.links.next = upper_addr;
        sf_footer* new_footer = (void*)next_block_ptr+next_block_size;
        *new_footer = (upper_addr->header)^sf_magic();
    }
    insert_to_freelist(upper_addr);
}

void remove_from_free(sf_block* fit_block){
        //printf("%p\n", fit_block);
        int minIndex = getIndex((size_t)(fit_block->header)&BLOCK_SIZE_MASK);
        sf_block* delNext = fit_block->body.links.next;
        sf_block* delPrev = fit_block->body.links.prev;
        if(sf_free_list_heads[minIndex].body.links.next==sf_free_list_heads[minIndex].body.links.prev){//remove the only one block
            sf_free_list_heads[minIndex].body.links.next=&sf_free_list_heads[minIndex];
            sf_free_list_heads[minIndex].body.links.prev=&sf_free_list_heads[minIndex];
        }
        else{ //remove the fit block
            (fit_block->body.links.prev)->body.links.next = delNext;
            (fit_block->body.links.next)->body.links.prev = delPrev;
        }
}

int getIndex(size_t block_size){
    int starting_index =0;
    if(block_size!=32){
        for(int i=1;i<8;i++){
            if(block_size>(int)pow(2,i-1)*32&&block_size<=(int)pow(2,i)*32){
                starting_index = i;
                break;
            }
        }
        if(starting_index==0)
            starting_index = 8;
    }
    return starting_index;
}

sf_block* search_for_fit(int minIndex, int size){
    for(int i=minIndex;i<NUM_FREE_LISTS;i++){
        sf_block* cur_block = (sf_free_list_heads[i].body.links.next);
        //while(cur_block!=&sf_free_list_heads[i]){
        while(cur_block->header!=0){
            if((cur_block->header&BLOCK_SIZE_MASK)>=size)
                return cur_block;
            cur_block = cur_block->body.links.next;
        }
    }
    return NULL;
}

void insert_to_freelist(sf_block* to_insert){
    int starting_index = getIndex((size_t)to_insert->header&BLOCK_SIZE_MASK);
    if((uintptr_t)sf_free_list_heads[starting_index].body.links.prev==(uintptr_t)&sf_free_list_heads[starting_index]&&(uintptr_t)sf_free_list_heads[starting_index].body.links.next==(uintptr_t)&sf_free_list_heads[starting_index]){
        sf_free_list_heads[starting_index].body.links.next = to_insert;
        sf_free_list_heads[starting_index].body.links.prev = to_insert;
        to_insert->body.links.next = &sf_free_list_heads[starting_index];
        to_insert->body.links.prev = &sf_free_list_heads[starting_index];
    }
    else{//at least one block
        sf_block* head = &sf_free_list_heads[starting_index];
        sf_block* old_head = sf_free_list_heads[starting_index].body.links.next;//real head
        old_head->body.links.prev = to_insert;
        head->body.links.next = to_insert;
        to_insert->body.links.next = old_head;
        to_insert->body.links.prev = head;
    }//rewrite next block's prev alloc, header and footer
    //just added
    sf_block* next = (void*)to_insert+(to_insert->header&BLOCK_SIZE_MASK);
    if((next->header&BLOCK_SIZE_MASK)!=0&&((uintptr_t)(void*)next+16)!=(uintptr_t)sf_mem_end()){
        next->header &= 0xfffffffe;//next block's prev alloc to 0
        sf_footer* next_foot = (void*)next+(next->header&BLOCK_SIZE_MASK);
        *next_foot = next->header^sf_magic();
    }
}

void sf_free(void *pp) {
    if(pp==NULL)
        abort();
    if(invalidPointer(pp))
        abort();
    sf_header* tofree_header = (sf_header*)pp-1;
    sf_footer* tofree_prevfooter = (sf_footer*)tofree_header-1;
    sf_footer* tofree_footer = (void*)tofree_prevfooter+((*tofree_header)&BLOCK_SIZE_MASK);
    sf_block* tofree = (sf_block*)tofree_prevfooter;
    *tofree_header&=0xfffffffd;
    tofree->body.links.next = tofree;
    tofree->body.links.prev = tofree;
    *tofree_footer = *tofree_header^sf_magic();
    coalesce(tofree);
    return;
}

int invalidPointer(void* pp){ //pp is payload
    sf_header* tofree_header = (sf_header*)pp-1;
    sf_footer* tofree_prevfooter = (sf_footer*)tofree_header-1;
    sf_footer* tofree_footer = (void*)tofree_prevfooter+((*tofree_header)&BLOCK_SIZE_MASK);
    sf_prologue* tofree_prologue = sf_mem_start();
    sf_footer* tofree_prologue_end = (sf_footer*)&tofree_prologue->footer+1;
    sf_epilogue* tofree_epilogue = (sf_epilogue*)sf_mem_end()-1;
    if((uintptr_t)tofree_header<(uintptr_t)tofree_prologue_end||
        (uintptr_t)tofree_footer>=(uintptr_t)tofree_epilogue||
        ((*tofree_header)&THIS_BLOCK_ALLOCATED)==0||
        ((*tofree_header)&BLOCK_SIZE_MASK)<32||
        ((((*tofree_prevfooter^sf_magic())&THIS_BLOCK_ALLOCATED)==THIS_BLOCK_ALLOCATED)&&((*tofree_header)&PREV_BLOCK_ALLOCATED)==0)||
        ((((*tofree_prevfooter^sf_magic())&THIS_BLOCK_ALLOCATED)==0)&&((*tofree_header)&PREV_BLOCK_ALLOCATED)==PREV_BLOCK_ALLOCATED)||
        (*tofree_footer)!=((*tofree_header)^sf_magic()))
    {
        return 1;
    }
    return 0;
}

void *sf_realloc(void *pp, size_t rsize) {
    if(pp==NULL){
        sf_errno = EINVAL;
        return NULL;
    }
    if(invalidPointer(pp)){
        sf_errno = EINVAL;
        return NULL;
    }
    sf_header* pp_header = (sf_header*)pp-1;
    sf_footer* pp_prevfooter = (sf_footer*)pp_header-1;
    sf_block* pp_block = (sf_block*)pp_prevfooter;
    int pp_size = ((*pp_header)&BLOCK_SIZE_MASK);
    if(rsize==0){//if(pp_size==0){
        sf_free((void*)&pp_block->body.payload);
        return NULL;
    }
    size_t pad = (16-((rsize+16)%16));
    if(pad==16)
        pad = 0;
    size_t r_size = rsize+16+pad;
    if((size_t)pp_size==(size_t)r_size)//if realloc equal size
        return pp;
    if((size_t)pp_size<(size_t)r_size){//realloc to larger size
        void* larger_malloc = sf_malloc(rsize);
        if(larger_malloc==NULL)
            return NULL;
        memcpy(larger_malloc,pp,rsize);
        sf_free(pp);
        return larger_malloc;
    }
    if((size_t)pp_size>(size_t)r_size){//realloc to small size
        int remain_size = pp_size-r_size;
        if(remain_size<32){//dont spilt
            return pp;
        }
        else{//split
            *pp_header = ((int)pp_size-(int)remain_size)|THIS_BLOCK_ALLOCATED|(*pp_header&PREV_BLOCK_ALLOCATED);
            sf_footer* new_pp_footer = (void*)pp_prevfooter+(pp_size-remain_size);
            *new_pp_footer = *pp_header^sf_magic();

            sf_block* split_free_block = (void*)pp_prevfooter+(pp_size-remain_size);
            split_free_block->header = (remain_size)|PREV_BLOCK_ALLOCATED;
            split_free_block->body.links.next = split_free_block;
            split_free_block->body.links.prev = split_free_block;
            sf_footer* split_free_footer = (void*)split_free_block+(split_free_block->header&BLOCK_SIZE_MASK);
            *split_free_footer = split_free_block->header^sf_magic();
            coalesce(split_free_block);
            return pp;
        }
    }
    return NULL;
}
