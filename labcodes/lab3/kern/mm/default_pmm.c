#include <pmm.h>
#include <list.h>
#include <string.h>
#include <default_pmm.h>

/*  In the First Fit algorithm, the allocator keeps a list of free blocks
 * (known as the free list). Once receiving a allocation request for memory,
 * it scans along the list for the first block that is large enough to satisfy
 * the request. If the chosen block is significantly larger than requested, it
 * is usually splitted, and the remainder will be added into the list as
 * another free block.
 *  Please refer to Page 196~198, Section 8.2 of Yan Wei Min's Chinese book
 * "Data Structure -- C programming language".
*/
// LAB2 EXERCISE 1: YOUR CODE
// you should rewrite functions: `default_init`, `default_init_memmap`,
// `default_alloc_pages`, `default_free_pages`.
/*
 * Details of FFMA
 * (1) Preparation:
 *  In order to implement the First-Fit Memory Allocation (FFMA), we should
 * manage the free memory blocks using a list. The struct `free_area_t` is used
 * for the management of free memory blocks.
 *  First, you should get familiar with the struct `list` in list.h. Struct
 * `list` is a simple doubly linked list implementation. You should know how to
 * USE `list_init`, `list_add`(`list_add_after`), `list_add_before`, `list_del`,
 * `list_next`, `list_prev`.
 *  There's a tricky method that is to transform a general `list` struct to a
 * special struct (such as struct `page`), using the following MACROs: `le2page`
 * (in memlayout.h), (and in future labs: `le2vma` (in vmm.h), `le2proc` (in
 * proc.h), etc).
 * (2) `default_init`:
 *  You can reuse the demo `default_init` function to initialize the `free_list`
 * and set `nr_free` to 0. `free_list` is used to record the free memory blocks.
 * `nr_free` is the total number of the free memory blocks.
 * (3) `default_init_memmap`:
 *  CALL GRAPH: `kern_init` --> `pmm_init` --> `page_init` --> `init_memmap` -->
 * `pmm_manager` --> `init_memmap`.
 *  This function is used to initialize a free block (with parameter `addr_base`,
 * `page_number`). In order to initialize a free block, firstly, you should
 * initialize each page (defined in memlayout.h) in this free block. This
 * procedure includes:
 *  - Setting the bit `PG_property` of `p->flags`, which means this page is
 * valid. P.S. In function `pmm_init` (in pmm.c), the bit `PG_reserved` of
 * `p->flags` is already set.
 *  - If this page is free and is not the first page of a free block,
 * `p->property` should be set to 0.
 *  - If this page is free and is the first page of a free block, `p->property`
 * should be set to be the total number of pages in the block.
 *  - `p->ref` should be 0, because now `p` is free and has no reference.
 *  After that, We can use `p->page_link` to link this page into `free_list`.
 * (e.g.: `list_add_before(&free_list, &(p->page_link));` )
 *  Finally, we should update the sum of the free memory blocks: `nr_free += n`.
 * (4) `default_alloc_pages`:
 *  Search for the first free block (block size >= n) in the free list and reszie
 * the block found, returning the address of this block as the address required by
 * `malloc`.
 *  (4.1)
 *      So you should search the free list like this:
 *          list_entry_t le = &free_list;
 *          while((le=list_next(le)) != &free_list) {
 *          ...
 *      (4.1.1)
 *          In the while loop, get the struct `page` and check if `p->property`
 *      (recording the num of free pages in this block) >= n.
 *              struct Page *p = le2page(le, page_link);
 *              if(p->property >= n){ ...
 *      (4.1.2)
 *          If we find this `p`, it means we've found a free block with its size
 *      >= n, whose first `n` pages can be malloced. Some flag bits of this page
 *      should be set as the following: `PG_reserved = 1`, `PG_property = 0`.
 *      Then, unlink the pages from `free_list`.
 *          (4.1.2.1)
 *              If `p->property > n`, we should re-calculate number of the rest
 *          pages of this free block. (e.g.: `le2page(le,page_link))->property
 *          = p->property - n;`)
 *          (4.1.3)
 *              Re-caluclate `nr_free` (number of the the rest of all free block).
 *          (4.1.4)
 *              return `p`.
 *      (4.2)
 *          If we can not find a free block with its size >=n, then return NULL.
 * (5) `default_free_pages`:
 *  re-link the pages into the free list, and may merge small free blocks into
 * the big ones.
 *  (5.1)
 *      According to the base address of the withdrawed blocks, search the free
 *  list for its correct position (with address from low to high), and insert
 *  the pages. (May use `list_next`, `le2page`, `list_add_before`)
 *  (5.2)
 *      Reset the fields of the pages, such as `p->ref` and `p->flags` (PageProperty)
 *  (5.3)
 *      Try to merge blocks at lower or higher addresses. Notice: This should
 *  change some pages' `p->property` correctly.
 */
free_area_t free_area;

#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)

static void
default_init(void) {
    list_init(&free_list);
    nr_free = 0;
}

static void
default_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        // page_init调用时，把所有的page都设置为内核保留状态了
        assert(PageReserved(p));
        // 因为这个函数是初始化空闲内存用的，所以把PG_reserved和PG_property都置0
        p->flags = p->property = 0;
        // 将ref也置0
        set_page_ref(p, 0);
    }

    base->property = n;
    // 将这一块连续内存空间的第一个页的PG_property置1表示空闲
    SetPageProperty(base);
    // 总的空闲页数+n
    nr_free += n;
    // 把这个连续空闲的内存块加入到空闲区域链表中
    // 每次将新的高地址插在表尾
    list_add(list_prev(&free_list), &(base->page_link));
}

static struct Page *
default_alloc_pages(size_t n) {
    // 先检查n是不是大于0了
    assert(n > 0);
    // n大于所有空闲页面的总和直接返回NULL
    if (n > nr_free) {
        return NULL;
    }
    //要返回的page指针，默认为NULL
    struct Page *page = NULL;
    //取得free_list的头节点
    list_entry_t *le = &free_list;
    //每次将le指向下一个节点的le，当发现回到表头时退出循环
    while ((le = list_next(le)) != &free_list) {
        //用le2page宏取得le对应的Page结构体的指针
        struct Page *p = le2page(le, page_link);
        //若当前的p满足要分配的大小，将p赋值给page变量并返回
        if (p->property >= n) {
            page = p;
            break;
        }
    }
    //退出循环时，若page不为空则进行分配
    if (page != NULL) {
        //先把当前的page从链表中删除
        // list_del(&(page->page_link));
        //空闲页大于n的情况
        if (page->property > n) {
            struct Page *p = page + n;
            // 将p的flag的property位置为1表示空闲内存块的首页，property设置为p后连续空闲页的大小
            SetPageProperty(p);
            p->property = page->property - n;


            //将p加入到空闲区链表中
            list_add(&(page->page_link), &(p->page_link));

            // cprintf("in alloc func, the p's ref value is: %d\n", page_ref(p));
            // cprintf("in alloc func, the page's ref value is: %d\n", page_ref(page));
        }

        // 总空闲页数-n
        nr_free -= n;
        //将包括page在内的n页全部分配出去，page的property应该变成0表示已占用
        page->property = 0;
        ClearPageProperty(page);
        list_del(&(page->page_link));
        // 让page的prev和next都指向自己
        // list_init(&page->page_link);
    }
    return page;
}

static void
default_free_pages(struct Page *base, size_t n) {
    //要释放的页面数应该大于0
    assert(n > 0);
    // if(!PageProperty(base)){
    //   cprintf("free failed! property bit = 0!")
    //   return;
    // }
    struct Page *p = base;
    for (; p != base + n; p ++) {
        // PG_reserved和PG_property位都应该为0，表示当前页可已被占用
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        // 引用数置0
        set_page_ref(p, 0);
    }
    //因为释放了n个页，property属性置为n，作为首页，property位置1
    base->property = n;
    SetPageProperty(base);

    list_entry_t *le = list_next(&free_list);
    while (le != &free_list) {
        p = le2page(le, page_link);
        //以下是两种合并情况
        //如果base的末尾和p的起始重合
        if (base + base->property == p) {
            // 将base设置为第一个page
            base->property += p->property;
            p->property = 0;
            ClearPageProperty(p);
            //将base添加到p之前
            list_add(list_prev(&p->page_link), &(base->page_link));
            //将p删去即可
            list_del(&(p->page_link));

            break;
        }
        //如果p的末尾和base的起始重合
        else if (p + p->property == base) {
            // 将p设置为该内存块的第一个page
            p->property += base->property;
            // 由于p已经在链表中，只要修改p的属性就行，然后将base变为普通的page
            base->property = 0;
            ClearPageProperty(base);
            //如果此时p的边界又和下一个page重合，则它们应该合并
            struct Page* nextPage = le2page(list_next(le), page_link);
            if(p + p->property == nextPage){
                p->property += nextPage->property;
                nextPage->property = 0;
                ClearPageProperty(nextPage);
                list_del(list_next(le));
            }
            break;

        }
        //第三种情况就是都没找到
        else {

        }
        le = list_next(le);

    }
    // 没有可合并的块
    if(le == &free_list){
      le = list_next(&free_list);
      while(le != &free_list){
        if(le2page(le, page_link) > base){
          list_add(list_prev(le), &(base->page_link));
          break;
        }
        le = list_next(le);
      }
      if(le == &free_list){
        list_add(list_prev(&free_list), &(base->page_link));
      }

    }

    nr_free += n;
}

static size_t
default_nr_free_pages(void) {
    return nr_free;
}

static void
basic_check(void) {
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;
    //不传参数有个宏定义#define alloc_page() alloc_pages(1) 在pmm.h中
    // p0、p1、p2都不应该为空
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    // p0、p1、p2应该两两不同
    assert(p0 != p1 && p0 != p2 && p1 != p2);
    // 它们的ref值都应该是0
    // cprintf("page_ref(p0) = %08x\n", &p0->ref);
    // cprintf("page_ref(p1) = %08x\n", page_ref(p1));
    // cprintf("page_ref(p2) = %08x\n", page_ref(p2));
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    // 它们的物理地址应该在管辖的范围内
    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    // 下面是占用页的释放
    // 首先保存空闲链表的头节点
    list_entry_t free_list_store = free_list;
    // 将头节点初始化
    list_init(&free_list);
    assert(list_empty(&free_list));

    // 保存空闲页的数量的值
    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    // 如果没有空闲页，则应该返回NULL
    assert(alloc_page() == NULL);

    // 释放p0、p1、p2
    free_page(p0);
    free_page(p1);
    free_page(p2);
    assert(nr_free == 3);

    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(alloc_page() == NULL);

    free_page(p0);
    assert(!list_empty(&free_list));

    struct Page *p;
    assert((p = alloc_page()) == p0);
    assert(alloc_page() == NULL);

    assert(nr_free == 0);
    free_list = free_list_store;
    nr_free = nr_free_store;

    free_page(p);
    free_page(p1);
    free_page(p2);
}

// LAB2: below code is used to check the first fit allocation algorithm (your EXERCISE 1) 
// NOTICE: You SHOULD NOT CHANGE basic_check, default_check functions!
static void
default_check(void) {
    int count = 0, total = 0;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count ++, total += p->property;
    }
    //检查所有page的property纸鹤是不是等于free_area中的nr_free
    assert(total == nr_free_pages());

    basic_check();

    struct Page *p0 = alloc_pages(5), *p1, *p2;
    // cprintf("line336 p0's address: %08x \n", p0);
    assert(p0 != NULL);
    assert(!PageProperty(p0));

    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));
    assert(alloc_page() == NULL);

    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    free_pages(p0 + 2, 3);
    assert(alloc_pages(4) == NULL);
    assert(PageProperty(p0 + 2) && p0[2].property == 3);
    assert((p1 = alloc_pages(3)) != NULL);
    assert(alloc_page() == NULL);
    assert(p0 + 2 == p1);
    //此时p0到p0+5都是占用状态

    // pmm.h中的宏 #define free_page(page) free_pages(page, 1)
    p2 = p0 + 1;
    // cprintf("line360 p1's address: %08x \n", p1);
    // cprintf("line361 p2's address: %08x \n", p2);
    free_page(p0);
    free_pages(p1, 3);//此时只有p2是占用状态
    assert(PageProperty(p0) && p0->property == 1);
    assert(PageProperty(p1) && p1->property == 3);

    p0 = alloc_page();

    assert((p0) == p2 - 1);
    free_page(p0);
    assert((p0 = alloc_pages(2)) == p2 + 1);//因为只有p1位置有3个页，所以分配以后p0=p1

    free_pages(p0, 2);
    free_page(p2);

    // cprintf("line380 nr_free: %08x ", nr_free);
    assert((p0 = alloc_pages(5)) != NULL);
    assert(alloc_page() == NULL);

    assert(nr_free == 0);
    nr_free = nr_free_store;

    free_list = free_list_store;
    free_pages(p0, 5);

    le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        count --, total -= p->property;
    }
    assert(count == 0);
    assert(total == 0);
}

const struct pmm_manager default_pmm_manager = {
    .name = "default_pmm_manager",
    .init = default_init,
    .init_memmap = default_init_memmap,
    .alloc_pages = default_alloc_pages,
    .free_pages = default_free_pages,
    .nr_free_pages = default_nr_free_pages,
    .check = default_check,
};

