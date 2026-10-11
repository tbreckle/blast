fn main() {
    // Compile the Slint UI; the generated code is included via slint::include_modules!().
    if let Err(e) = slint_build::compile("ui/app.slint") {
        panic!("Failed to compile ui/app.slint: {e}");
    }

    // CI injects the version computed by scripts/version.sh. Cargo.toml stays at 0.0.0, so
    // local builds are marked as unofficial.
    println!("cargo:rerun-if-env-changed=BLAST_VERSION");
    let version = std::env::var("BLAST_VERSION")
        .ok()
        .filter(|v| !v.is_empty())
        .unwrap_or_else(|| env!("CARGO_PKG_VERSION").to_string());
    println!("cargo:rustc-env=BLAST_VERSION={version}");

    // Embed the application icon and version info into blast.exe (shown in Explorer, the
    // taskbar, shortcuts and the file properties). CARGO_CFG_TARGET_OS is the target, not the
    // host, so this only runs for Windows builds.
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows") {
        println!("cargo:rerun-if-changed=../images/blast.ico");
        let mut res = winresource::WindowsResource::new();
        res.set_icon("../images/blast.ico")
            .set("ProductName", "B.L.A.S.T.")
            .set("FileDescription", "B.L.A.S.T. Configuration Tool")
            .set("ProductVersion", &version)
            .set("FileVersion", &version);
        // The numeric version only takes X.Y.Z; pre-release and build suffixes are dropped.
        let mut parts = version
            .split(['-', '+'])
            .next()
            .unwrap_or_default()
            .split('.')
            .map(|p| p.parse::<u64>().unwrap_or(0));
        let numeric = (0..3).fold(0u64, |acc, i| {
            acc | (parts.next().unwrap_or(0) << (48 - 16 * i))
        });
        res.set_version_info(winresource::VersionInfo::FILEVERSION, numeric)
            .set_version_info(winresource::VersionInfo::PRODUCTVERSION, numeric);
        if let Err(e) = res.compile() {
            panic!("Failed to embed the Windows resources: {e}");
        }
    }
}
