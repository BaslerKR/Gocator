#ifndef GOAPI_PROPERTIES_UDI_CACHE_H
#define GOAPI_PROPERTIES_UDI_CACHE_H

#include <memory>

#include <GoApi/GoApiDef.h>
#include <GoApi/Object.h>
#include <GoApi/Threads/Locker.h>

namespace Go
{
namespace UserData
{
#define UDICACHE_DATA_SIZE (1000)

class GoApiClass UdiData
{
public:
    /**
    * Constructor for UdiData. Note that this class should not be manually constructed.
    */
    UdiData(void* data, kSize size);
    ~UdiData();

    /**
    * Checks if the provided offset and size will be able to get data 
    * within the buffer without exceeding bounds.
    * 
    * @param offset     The address offset in the memory space.
    * @param size       The size of the data expected to be accesssed. Use kSize_Max to get all data.
    */
    bool CanGet(kSize offset, kSize size) const;

    /**
    * Retrieves a pointer to an offset location in the data.
    *
    * @param offset     The address offset in the memory space.
    * @param size       The size of the data expected to be accesssed. Use kSize_Max to get all data.
    *
    * @throws           Go::Exception if offset+size would exceed the maximum available data.
    * @remarks          This function works on the honor system. The caller should provide the offset
                        and the size they want to pull a value of. For example, a user might say 
                        offset=84, size=8 for pulling a k64u. The function will throw an exception 
                        if this value would be off the end of the avilable data.
    */
    kByte* Get(kSize offset, kSize size) const;

private:
    kByte* m_data;
    kSize m_dataSize;
};

class GoApiClass UdiCache
{
private:
    UdiCache();
public:
    /**
    * Gets (and creates if first call) the singleton instance.
    */
    static UdiCache& GetInstance();

    /**
    * Destroys the singleton instance.
    */
    static void DestroyInstance();
    UdiCache(UdiCache const&) = delete;
    void operator=(UdiCache const&) = delete;

    /**
    * Locks the read/write lock. This should be called if multiple writes will be 
    * occurring in succession that should be part of the same lock.
    * 
    * @return       Returns a Locker object that will act as a RAII scoped lock.
    */
    Go::Locker BeginWrite();

    /**
    * Writes data to the cache. Thread safe with GetData().
    * This data will be copied into the cache. Two copies will be performed.
    * 
    * @param data       Pointer to the data to be copied.
    * @param offset     Offset into the cache to copy the data into.
    * @param size       Size of the data to be copied.
    * 
    * @throws           Go::Exception if offset+size would exceed the maximum available cache space.
    */
    void Write(void* data, kSize offset, kSize size);

    /**
    * Thread-safe access to the cache data. Returns a snapshot (through a shared_ptr to the current state)
    * of the cache data.
    */
    std::shared_ptr<UdiData> GetData() const;

private:
    Go::Object<kLock> m_lock;
    std::shared_ptr<UdiData> m_sharedCopy;
    kByte m_buffer[UDICACHE_DATA_SIZE];
};
}
} //Namespaces

#endif