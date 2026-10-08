



#[macro_export]
macro_rules! vb_log {
    ($prefix:expr, $($arg:tt)*) => {
        println!("{} {} {}", $crate::debug::ts(), $prefix, format!($($arg)*));
    };
}