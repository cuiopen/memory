#ifndef FOONATHAN_MEMORY_STACK_ALLOCATOR_HPP_INCLUDED
#define FOONATHAN_MEMORY_STACK_ALLOCATOR_HPP_INCLUDED

/// \file
/// \brief Stack allocators.

#include <cassert>
#include <cstdint>
#include <type_traits>

#include "detail/block_list.hpp"
#include "heap_allocator.hpp"
#include "raw_allocator_base.hpp"

namespace foonathan { namespace memory
{    
    /// \brief A memory stack.
    ///
    /// Allows fast memory allocations but deallocation is only possible via markers.
    /// All memory after a marker is then freed, too.<br>
    /// It allocates big blocks from an implementation allocator.
    /// If their size is sufficient, allocations are fast.
    /// \ingroup memory
    template <class RawAllocator = heap_allocator>
    class memory_stack : RawAllocator
    {
    public:
        /// \brief The implementation allocator.
        using impl_allocator = RawAllocator;
    
        /// \brief Constructs it with a given start block size.
        /// \detail The first memory block is allocated, the block size can change.
        explicit memory_stack(std::size_t block_size,
                        impl_allocator allocator = impl_allocator())
        : list_(block_size, std::move(allocator))
        {
            allocate_block();
        }
        
        /// \brief Allocates a memory block of given size and alignment.
        /// \detail If it does not fit into the current block, a new one will be allocated.
        /// The new block must be big enough for the requested memory.
        void* allocate(std::size_t size, std::size_t alignment)
        {
            auto offset = align_offset(alignment);
            if (offset + size > capacity())
            {
                allocate_block();
                offset = align_offset(alignment);
                assert(offset + size <= capacity() && "block size too small");
            }
            // now we have sufficient size
            cur_ += offset; // align
            auto memory = cur_;
            cur_ += size; // bump
            return memory;
        }
        
        /// \brief Marker type for unwinding.
        class marker
        {
            std::size_t index;
            // store both and cur_end to replicate state easily
            char *cur, *cur_end;
            
            marker(std::size_t i, char *cur, char *cur_end) noexcept
            : index(i), cur(cur), cur_end(cur_end) {}
        };
        
        /// \brief Returns a marker to the current top of the stack.
        marker top() const noexcept
        {
            return {list_.size() - 1, cur_, cur_end_};
        }
        
        /// \brief Unwinds the stack to a certain marker.
        /// \detail It must be less than the previous one.
        /// Any access blocks are freed.
        void unwind(marker m) noexcept
        {
            auto diff = list_.size() - m.index - 1;
            for (auto i = 0u; i != diff; ++i)
                list_.deallocate();
            cur_     = m.cur_;
            cur_end_ = m.cur_end_;
        }
        
        /// \brief Returns the capacity remaining in the current block.
        std::size_t capacity() const noexcept
        {
            return cur_end_ - cur_;
        }
        
        /// \brief Returns the size of the memory block available after the capacity() is exhausted.
        std::size_t next_capacity() const noexcept
        {
            return list_.next_block_size();
        }
        
    private:
        std::size_t align_offset(std::size_t alignment) const noexcept
        {
            auto address = reinterpret_cast<std::uintptr_t>(cur_);
            auto misaligned = address & (alignment - 1);
            // misaligned != 0 ? (alignment - misaligned) : 0
            return misaligned * (alignment - misaligned);
        }
        
        void allocate_block()
        {
            auto block = list_.allocate();
            cur_ = static_cast<char*>(block.memory);
            cur_end_ = cur_ + block.size;
        }
    
        detail::block_list<impl_allocator> list_;
        char *cur_, *cur_end_;
    };
    
    /// \brief Allocator interface for the \ref memory_stack.
    /// \ingroup memory
    template <class ImplRawAllocator = heap_allocator>
    class stack_allocator : public raw_allocator_base<stack_allocator<ImplRawAllocator>>
    {
    public:
        using is_stateful = std::true_type;
        
        /// \brief Construct it giving a reference to the \ref memory_stack it uses.
        stack_allocator(memory_stack<ImplRawAllocator> &stack) noexcept
        : stack_(&stack) {}
        
        /// \brief Allocation function forwards to the stack for array and node.
        void* allocate_node(std::size_t size, std::size_t alignment)
        {
            return stack_->allocate(size, alignment);
        }
        
        /// \brief Deallocation function does nothing, use unwinding on the stack to free memory.
        void deallocate_node(void *, std::size_t, std::size_t) noexcept {}
        
        /// @{
        /// \brief The maximum size is the equivalent of the \ref next_capacity().
        std::size_t max_node_size() const noexcept
        {
            return stack_->next_capacity();
        }
        
        std::size_t max_array_size() const noexcept
        {
            return stack_->next_capacity();
        }
        /// @}
        
        /// @{
        /// \brief Returns a reference to the \ref memory_stack it uses.
        memory_stack<ImplRawAllocator>& get_memory() noexcept
        {
            return *stack_;
        }
        
        const memory_stack<ImplRawAllocator>& get_memory() const noexcept
        {
            return *stack_;
        }
        /// @}
        
    private:
        memory_stack<ImplRawAllocator> *stack_;
    };
}} // namespace foonathan::memory

#endif // FOONATHAN_MEMORY_STACK_ALLOCATOR_HPP_INCLUDED
