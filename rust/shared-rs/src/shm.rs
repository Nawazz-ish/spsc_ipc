use libc::{c_void, close, ftruncate, mmap, shm_open};
use libc::{MAP_FAILED, MAP_SHARED, O_CREAT, O_RDWR, PROT_READ, PROT_WRITE};
use std::ffi::CString;
use std::ptr;

pub fn shm_attach(name: &str, size: usize, create: bool) -> *mut c_void {
    let cname = CString::new(name).expect("shm name has interior null byte");

    let mut flags = O_RDWR;
    if create {
        flags |= O_CREAT;
    }

    let fd = unsafe { shm_open(cname.as_ptr(), flags, 0o600) };
    if fd < 0 {
        panic!("shm_open failed: {}", std::io::Error::last_os_error());
    }

    if create {
        let rc = unsafe { ftruncate(fd, size as i64) };
        if rc < 0 {
            panic!("ftruncate failed: {}", std::io::Error::last_os_error());
        }
    }

    let p = unsafe {
        mmap(
            ptr::null_mut(),
            size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd,
            0,
        )
    };
    if p == MAP_FAILED {
        panic!("mmap failed: {}", std::io::Error::last_os_error());
    }

    unsafe { close(fd); }
    p
}
