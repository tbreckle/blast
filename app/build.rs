fn main() {
    // Compile the Slint UI; the generated code is included via slint::include_modules!().
    if let Err(e) = slint_build::compile("ui/app.slint") {
        panic!("Failed to compile ui/app.slint: {e}");
    }
}
