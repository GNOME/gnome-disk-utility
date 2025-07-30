/// An RAII buffer that is aligned to the page size of the system.
pub struct PageAlignedBuffer {
    /// Memory layout of the buffer.
    layout: std::alloc::Layout,
    /// Pointer to the page aligned memory.
    buffer_ptr: *mut u8,
    /// Size of the allocated memory in bytes.
    size: usize,
}

impl PageAlignedBuffer {
    /// Allocates a new buffer with aligned to the page size of the operating system.
    ///
    /// The allocated memory will be zeroed.
    ///
    /// # Panics
    ///
    /// This function panics, the memory required for the buffer could not be allocated.
    ///
    /// # Examples
    /// ```ignore
    /// # use gnome_disks::page_aligned_buffer::PageAlignedBuffer;
    /// let mut buffer = PageAlignedBuffer::new(42);
    /// assert_eq!(buffer.as_mut_slice().len(), 42);
    /// ```
    #[must_use]
    pub fn new(size: usize) -> Self {
        unsafe {
            let page_size = libc::sysconf(libc::_SC_PAGESIZE) as usize;
            let layout = std::alloc::Layout::from_size_align(size, page_size)
                .expect("Failed to create layout for buffer");
            assert!(layout.size() >= size, "Layout is smaller than requested");

            let buffer_ptr = std::alloc::alloc_zeroed(layout);
            assert!(!buffer_ptr.is_null(), "Failed to allocate buffer memory");

            Self {
                layout,
                buffer_ptr,
                size,
            }
        }
    }

    /// Returns a mutable slice of the allocated buffer.
    ///
    /// # Examples
    /// ```ignore
    /// # use gnome_disks::page_aligned_buffer::PageAlignedBuffer;
    /// let mut buffer = PageAlignedBuffer::new(42);
    /// let slice = buffer.as_mut_slice();
    /// slice[0] = 12;
    /// assert_eq!(slice[0], 12);
    /// ```
    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        // SAFETY: we know that the pointer is valid, correctly aligned and can hold at least
        // [`self.size`] bytes
        unsafe { std::slice::from_raw_parts_mut(self.buffer_ptr, self.size) }
    }
}

impl Drop for PageAlignedBuffer {
    fn drop(&mut self) {
        unsafe {
            // SAFETY: as we only ever give out an exclusive reference to the buffer and it's not
            // possible to clone, we can be sure that there exists no other reference to the
            // buffer.
            // As the buffer cannot be swapped out, it was created from self.layout.
            std::alloc::dealloc(self.buffer_ptr, self.layout);
        }
    }
}
