#[repr(C)]
#[derive(Clone, Copy)]
pub struct Message {
    pub tsc: u64,
    pub seq: u64,
    pub pad: [u8; 48],
}

impl Default for Message {
    fn default() -> Self {
        Self {
            tsc: 0,
            seq: 0,
            pad: [0u8; 48],
        }
    }
}

const _: () = assert!(std::mem::size_of::<Message>() == 64);
