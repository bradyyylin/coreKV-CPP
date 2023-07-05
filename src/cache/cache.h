#pragma once
#include <stdint.h>

#include <functional>
#include <string>
#include <memory>
#include <vector>

#include "lru.h"

// 类似于Java的包 github.com/hardcore-os/coreKV-CPP
namespace corekv {
// 类似于Java的泛型 public interface Cache<KeyType, ValueType>
template <typename KeyType, typename ValueType>
class Cache {
 // 类中函数与变量的访问级别
 public:
  // 无参构造函数
  // 告诉编译器使用默认的实现来生成该函数的定义，而不需要手动提供函数体
  Cache() = default;
  // ~表示析构函数
  // virtual:将一个成员函数声明为虚函数时，它可以在派生类中以相同的函数签名进行重新定义，从而实现多态性
  // 这意味着当使用基类指针或引用指向派生类对象时，通过调用基类指针或引用的析构函数，可以正确地调用派生类的析构函数，确保释放派生类的资源 
  virtual ~Cache() = default;
  // 通过将函数定义为纯虚函数，将基类定义为抽象类(使用 = 0 表示该函数是纯虚函数)
  // const 表示是常量，不可修改
  // 第一个表示返回的指针指向的字符是常量 这意味着调用该函数返回的字符串应该被视为只读，并且不能通过返回的指针进行修改
  // 函数参数列表后面加入const表示该函数为常量函数，表示希望只读
  virtual const char* Name() const = 0;
  // 在缓存中加入一个键值对
  // const KeyType& key: 表示键的类型为 KeyType，通过引用传递，并声明为常量引用，以避免在函数内部修改传入的键对象。
  // ValueType* value: 表示值的类型为 ValueType 的指针。通过指针传递值对象的地址，以允许对值对象进行修改。
  // = 0表示这是一个纯虚函数 派生类必须实现这个函数
  virtual void Insert(const KeyType& key, ValueType* value,
                      uint32_t ttl = 0) = 0;
  virtual CacheNode<KeyType, ValueType>* Get(const KeyType& key) = 0;
  virtual void Release(CacheNode<KeyType, ValueType>* node) = 0;
  virtual void Prune() = 0;
  virtual void Erase(const KeyType& key) = 0;
  // 函数的目的是用于注册一个清理处理函数（clean handle）。清理处理函数用于在缓存中的某个对象被释放或删除时执行特定的操作，例如释放资源或执行其他清理操作。
  // 通过将一个函数对象作为参数传递给 RegistCleanHandle 函数，可以将这个函数对象注册为清理处理函数。派生类需要实现这个函数，并在适当的时候调用注册的清理处理函数来执行相应的清理操作
  virtual void RegistCleanHandle(
      std::function<void(const KeyType& key, ValueType* value)> destructor) = 0;
};

// 在这里使用分片方式来处理缓存
template <typename KeyType, typename ValueType>
// ShardCache是Cache的子类
class ShardCache final : public Cache<KeyType, ValueType> {
 public:
  ShardCache() = default;
  ShardCache(uint32_t capacity) {
    // 循环来初始化 cache_impl_ 数组中的每个元素
    for (int32_t index = 0; index < kShardNum; ++index) {
      // std::make_shared : 创建动态分配的对象并返回一个指向该对象的共享指针
      // 共享指针通过使用引用计数来跟踪对对象的引用次数。每当创建一个新的共享指针指向同一对象时，引用计数会增加。
      // 当某个共享指针超出其作用域、被重置或显式释放时，引用计数会相应减少。
      // 当引用计数为零时，即没有共享指针引用对象时，才会自动释放对象的内存。
      // 共享指针提供了方便的语法和自动内存管理，可以帮助避免常见的内存泄漏和悬空指针问题。
      // 它还支持自定义的删除器（deleter），允许在释放对象时执行特定的清理操作。

      // 创建了一个共享指针指向一个 LruCachePolicy 类的实例
      cache_impl_[index] = std::make_shared<LruCachePolicy<KeyType, ValueType, MutexLock>>(capacity);
    }
  }
  ~ShardCache() = default;
  const char* Name() const {
      return "shard.cache";
  }
  void Insert(const KeyType& key, ValueType* value, uint32_t ttl = 0) {
    // 寻址
    uint64_t shard_num = std::hash<KeyType>{}(key) % kShardNum;
    // 调用 cache_impl_[shard_num] 指针所指向的对象的 Insert 方法，将给定的参数传递给该方法
    cache_impl_[shard_num]->Insert(key, value, ttl);
  }
  CacheNode<KeyType, ValueType>* Get(const KeyType& key) {
    uint64_t shard_num = std::hash<KeyType>{}(key) % kShardNum;
    return cache_impl_[shard_num]->Get(key);
  }
  void Release(CacheNode<KeyType, ValueType>* node) {
    uint64_t shard_num = std::hash<KeyType>{}(node->key) % kShardNum;
    return cache_impl_[shard_num]->Release(node);
  }
  // 对所有的分片进行回收
  void Prune() {
    for (int32_t index = 0; index < kShardNum; ++index) {
      cache_impl_[index]->Prune();
    }
  }
  void Erase(const KeyType& key) {
    uint64_t shard_num = std::hash<KeyType>{}(key) % kShardNum;
    return cache_impl_[shard_num]->Erase(key);
  }
  void RegistCleanHandle(
      std::function<void(const KeyType& key, ValueType* value)> destructor) {
    for (int32_t index = 0; index < kShardNum; ++index) {
      cache_impl_[index]->RegistCleanHandle(destructor);
    }
  }

 private:
  // constexpr 表示编译器即赋值（优化项）
  static constexpr uint8_t kShardNum = 4;
  // 采用impl的机制来进行实现 一个List
  std::vector<std::shared_ptr<CachePolicy<KeyType, ValueType>>> cache_impl_;
};

}  
