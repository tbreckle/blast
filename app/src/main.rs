#![cfg_attr(target_os = "windows", windows_subsystem = "windows")]

mod protocol;
mod slip;
mod types;
mod ui;

use ui::BlastApp;

fn main() -> Result<(), eframe::Error> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([1200.0, 700.0])
            .with_title("B.L.A.S.T. Configuration Tool"),
        ..Default::default()
    };

    eframe::run_native(
        "B.L.A.S.T. Configuration Tool",
        options,
        Box::new(|cc| {
            let app = BlastApp::default();
            // Apply the saved theme on startup
            app.apply_theme(&cc.egui_ctx);
            Ok(Box::new(app))
        }),
    )
}
