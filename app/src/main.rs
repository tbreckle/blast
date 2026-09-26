#![cfg_attr(target_os = "windows", windows_subsystem = "windows")]

mod keymap;
mod protocol;
mod slip;
mod types;
mod ui;

fn main() -> anyhow::Result<()> {
    ui::run()
}
