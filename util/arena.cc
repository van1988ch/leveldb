// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "util/arena.h"

namespace leveldb {

//块大小
static const int kBlockSize = 4096;

Arena::Arena()
    : alloc_ptr_(nullptr), alloc_bytes_remaining_(0), memory_usage_(0) {}

Arena::~Arena() {
  for (size_t i = 0; i < blocks_.size(); i++) {
    delete[] blocks_[i];
  }
}

char* Arena::AllocateFallback(size_t bytes) {
  //在内存不够的情况下,
  if (bytes > kBlockSize / 4) {
    //大于块的1/4就按需分配，这样可以保证不浪费太多内存
    char* result = AllocateNewBlock(bytes);
    return result;
  }

  //如果需要的块的数据大于1/4的块大小的时候，但是之前的剩余的块空间又不够了，直接浪费掉之前的，分配一块新的
  // We waste the remaining space in the current block.
  alloc_ptr_ = AllocateNewBlock(kBlockSize);
  alloc_bytes_remaining_ = kBlockSize;

  char* result = alloc_ptr_;
  alloc_ptr_ += bytes;
  alloc_bytes_remaining_ -= bytes;
  return result;
}

char* Arena::AllocateAligned(size_t bytes) {
  const int align = (sizeof(void*) > 8) ? sizeof(void*) : 8;
  static_assert((align & (align - 1)) == 0,
                "Pointer size should be a power of 2");
  //计算对齐溢出了几个字节的内存，补齐
  size_t current_mod = reinterpret_cast<uintptr_t>(alloc_ptr_) & (align - 1);
  size_t slop = (current_mod == 0 ? 0 : align - current_mod);
  size_t needed = bytes + slop;
  char* result;
  if (needed <= alloc_bytes_remaining_) {
    result = alloc_ptr_ + slop;//对齐后浪费前面几个字节
    alloc_ptr_ += needed;
    alloc_bytes_remaining_ -= needed;
  } else {
    // AllocateFallback always returned aligned memory
    //初始分配始终是对齐的
    result = AllocateFallback(bytes);
  }
  assert((reinterpret_cast<uintptr_t>(result) & (align - 1)) == 0);
  return result;
}

//统计总共的内存
char* Arena::AllocateNewBlock(size_t block_bytes) {
  //relaxed ordering不会造成顺序同步，在单个线程类依然是遵循操作在前先执行的顺序，但是到其他线程里面则无法知道这种先后顺序即在1线程内A操作先于B操作，那麼在2线程里面执行B操作的时候它可能会认为A操作在此之前并没有被执行过。
  //Sequentially consistent ordering是atomic默认的操作选项，由于它暗含程序的行为对于atomic变量操作的一致性。如果atomic变量的所有操作顺序都确定了，那麼多线程程序行为就像在单线程内以特定的顺序执行这些操作即所有线程可以看到相同的操作执行顺序这也意味着操作不可以被reorder。通俗来说，如果1线程执行了A，B两个操作，然后2线程执行B操作，那麼此时2线程必定知道A操作被执行过了，因为这个操作顺序（A在B之前执行）是所有线程都能够看到的。
  char* result = new char[block_bytes];
  blocks_.push_back(result);
  memory_usage_.fetch_add(block_bytes + sizeof(char*),
                          std::memory_order_relaxed);//速度快。默认是memory_order_seq_cst，速度慢
  return result;
}

}  // namespace leveldb
