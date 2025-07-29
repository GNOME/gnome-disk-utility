/// An RAII buffer that is aligned to the page size of the system.
pub struct PageAlignedBuffer {
    /// Memory layout of the buffer.
    layout: std::alloc::Layout,
    /// Reference to the page aligned memory.
    buffer: &'static mut [u8],
}

impl PageAlignedBuffer {
    /// Allocates a new buffer with aligned to the page size of the operating system.
    /// The allocated memory will be zeroed.
    #[must_use]
    pub fn new(size: usize) -> Self {
        unsafe {
            let page_size = libc::sysconf(libc::_SC_PAGESIZE) as usize;
            let layout = std::alloc::Layout::from_size_align(size, page_size)
                .expect("Failed to create layout for buffer");
            assert!(layout.size() >= size, "Layout is smaller than requested");
            let buffer_ptr = std::alloc::alloc_zeroed(layout);
            assert!(!buffer_ptr.is_null(), "Failed to alloc buffer memory");
            // SAFETY: we know that the pointer is valid, correctly aligned and has space for `BUFFER_SIZE`
            // bytes
            let buffer = std::slice::from_raw_parts_mut(buffer_ptr, size);
            Self { layout, buffer }
        }
    }

    /// Returns a mutable slice of the allocated buffer.
    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        self.buffer
    }
}

impl Drop for PageAlignedBuffer {
    fn drop(&mut self) {
        unsafe {
            // SAFETY: as we only ever give out an exclusive reference to the buffer and it's not
            // possible to clone, we can be sure that there exists no other reference to the
            // buffer.
            // As the buffer cannot be swapped out, it was created from self.layout.
            std::alloc::dealloc(self.buffer.as_mut_ptr(), self.layout);
        }
    }
}
