use crate::protocol::FirmwareConnection;
use crate::types::{
    ButtonMapping, FirmwareVersion, KeyCombo, MOD_ALT, MOD_CTRL, MOD_ESC, MOD_F, MOD_SHIFT,
};
use eframe::egui;
use serde::{Deserialize, Serialize};
use serialport::{SerialPortInfo, SerialPortType};
use std::sync::mpsc;
use std::thread;
use std::time::Duration;

#[derive(Debug, Default, Clone, Copy, Serialize, Deserialize, PartialEq)]
pub enum Theme {
    #[default]
    System,
    Dark,
    Light,
}

impl Theme {
    fn as_str(&self) -> &'static str {
        match self {
            Theme::System => "System",
            Theme::Dark => "Dark",
            Theme::Light => "Light",
        }
    }
}

#[derive(Debug, Serialize, Deserialize)]
struct ThemeConfig {
    theme: Theme,
}

impl Default for ThemeConfig {
    fn default() -> Self {
        ThemeConfig {
            theme: Theme::System,
        }
    }
}

// Field indices for key capture.
const FIELD_START_P1: usize = 0;
const FIELD_START_P2: usize = 1;
const FIELD_COIN_P1: usize = 2;
const FIELD_COIN_P2: usize = 3;
const FIELD_ACTION_P1_1: usize = 4;
const FIELD_ACTION_P1_2: usize = 5;
const FIELD_ACTION_P2_1: usize = 6;
const FIELD_ACTION_P2_2: usize = 7;
const FIELD_SERVICE_P1: usize = 8;
const FIELD_SERVICE_P2: usize = 9;
const FIELD_TEST_P1: usize = 10;
const FIELD_TEST_P2: usize = 11;
const FIELD_PAUSE: usize = 12;
const FIELD_SAVE: usize = 13;
const FIELD_LOAD: usize = 14;
const FIELD_EXIT: usize = 15;

enum SyncResult {
    Connected {
        connection: FirmwareConnection,
        profiles: Vec<ButtonMapping>,
        version: FirmwareVersion,
        port_name: String,
    },
    Reloaded {
        connection: FirmwareConnection,
        profiles: Vec<ButtonMapping>,
    },
    Saved {
        connection: FirmwareConnection,
    },
    Rebooted,
    Error {
        connection: Option<FirmwareConnection>,
        message: String,
    },
}

pub struct BlastApp {
    // Connection state.
    serial_ports: Vec<SerialPortInfo>,
    selected_port: Option<String>,
    connection: Option<FirmwareConnection>,
    firmware_version: Option<FirmwareVersion>,

    // Profile data.
    profiles: Vec<ButtonMapping>,
    selected_profile_idx: Option<usize>,
    is_dirty: bool,

    // UI state.
    is_syncing: bool,
    sync_message: String,
    pending_result: Option<mpsc::Receiver<SyncResult>>,
    show_profile_editor: bool,
    show_delete_confirmation: bool,
    show_reload_confirmation: bool,
    delete_profile_idx: Option<usize>,
    editing_profile_idx: Option<usize>,
    editor_profile: ButtonMapping,

    // Profile editor state.
    capturing_field: Option<usize>,
    error_message: Option<String>,

    // Theme state.
    theme: Theme,
}

impl Default for BlastApp {
    fn default() -> Self {
        let serial_ports = serialport::available_ports().unwrap_or_default();

        // Load theme from config
        let theme = confy::load::<ThemeConfig>("blast", "config")
            .ok()
            .map(|config| config.theme)
            .unwrap_or(Theme::System);

        Self {
            serial_ports,
            selected_port: None,
            connection: None,
            firmware_version: None,
            profiles: Vec::new(),
            selected_profile_idx: None,
            is_dirty: false,
            is_syncing: false,
            sync_message: String::new(),
            pending_result: None,
            show_profile_editor: false,
            show_delete_confirmation: false,
            show_reload_confirmation: false,
            delete_profile_idx: None,
            editing_profile_idx: None,
            editor_profile: ButtonMapping::default(),
            capturing_field: None,
            error_message: None,
            theme,
        }
    }
}

impl BlastApp {
    fn refresh_ports(&mut self) {
        self.serial_ports = serialport::available_ports().unwrap_or_default();
    }

    fn connect_to_port(&mut self, port_name: String) {
        self.is_syncing = true;
        self.sync_message = "Connecting to device...".to_string();
        self.error_message = None;

        let port_name_clone = port_name.clone();
        let (tx, rx) = mpsc::channel();
        self.pending_result = Some(rx);

        thread::spawn(move || {
            let result = (|| -> Result<(FirmwareConnection, Vec<ButtonMapping>, FirmwareVersion), String> {
                let port = serialport::new(&port_name_clone, 115200)
                    .timeout(Duration::from_millis(100))
                    .open()
                    .map_err(|e| format!("Failed to open port: {}", e))?;

                let mut conn = FirmwareConnection::new(port);

                let version = conn.get_version()
                    .map_err(|e| format!("Failed to read firmware version: {}", e))?;

                let profiles = conn.get_all_profiles()
                    .map_err(|e| format!("Connection failed: Not a valid Blast device or communication error: {}", e))?;

                Ok((conn, profiles, version))
            })();

            let _ = tx.send(match result {
                Ok((connection, profiles, version)) => SyncResult::Connected {
                    connection,
                    profiles,
                    version,
                    port_name,
                },
                Err(message) => SyncResult::Error {
                    connection: None,
                    message,
                },
            });
        });
    }

    fn disconnect(&mut self) {
        self.connection = None;
        self.selected_port = None;
        self.firmware_version = None;
        self.profiles.clear();
        self.is_dirty = false;
    }

    fn reload_data(&mut self) {
        if let Some(conn) = self.connection.take() {
            self.is_syncing = true;
            self.sync_message = "Reloading data...".to_string();
            self.error_message = None;

            let (tx, rx) = mpsc::channel();
            self.pending_result = Some(rx);

            thread::spawn(move || {
                let mut conn = conn;
                let _ = tx.send(match conn.get_all_profiles() {
                    Ok(profiles) => SyncResult::Reloaded {
                        connection: conn,
                        profiles,
                    },
                    Err(e) => SyncResult::Error {
                        connection: Some(conn),
                        message: format!("Failed to reload profiles: {}", e),
                    },
                });
            });
        }
    }

    fn save_data(&mut self) {
        if let Some(conn) = self.connection.take() {
            self.is_syncing = true;
            self.sync_message = "Saving profiles...".to_string();
            self.error_message = None;

            let profiles = self.profiles.clone();
            let (tx, rx) = mpsc::channel();
            self.pending_result = Some(rx);

            thread::spawn(move || {
                let mut conn = conn;
                let result = (|| -> Result<(), String> {
                    let max_profiles = conn
                        .get_max_profiles()
                        .map_err(|e| format!("Failed to query max profiles: {}", e))?
                        as usize;

                    for (idx, profile) in profiles.iter().enumerate() {
                        if idx >= max_profiles {
                            return Err(format!(
                                "Too many profiles: {} exceeds firmware limit of {}.",
                                profiles.len(),
                                max_profiles
                            ));
                        }
                        conn.save_profile(idx as u8, profile)
                            .map_err(|e| format!("Failed to save profile {}: {}", idx, e))?;
                    }

                    for idx in profiles.len()..max_profiles {
                        let empty_profile = ButtonMapping::default();
                        conn.save_profile(idx as u8, &empty_profile)
                            .map_err(|e| format!("Failed to clear profile slot {}: {}", idx, e))?;
                    }

                    Ok(())
                })();

                let _ = tx.send(match result {
                    Ok(()) => SyncResult::Saved { connection: conn },
                    Err(message) => SyncResult::Error {
                        connection: Some(conn),
                        message,
                    },
                });
            });
        }
    }

    fn reboot_device(&mut self) {
        if let Some(conn) = self.connection.take() {
            self.is_syncing = true;
            self.sync_message = "Rebooting device...".to_string();
            self.error_message = None;

            let (tx, rx) = mpsc::channel();
            self.pending_result = Some(rx);

            thread::spawn(move || {
                let mut conn = conn;
                let _ = tx.send(match conn.reboot_flash() {
                    Ok(_) => SyncResult::Rebooted,
                    Err(e) => SyncResult::Error {
                        connection: Some(conn),
                        message: format!("Failed to reboot device: {}", e),
                    },
                });
            });
        }
    }

    fn poll_pending_result(&mut self) {
        if let Some(rx) = &self.pending_result {
            match rx.try_recv() {
                Ok(result) => {
                    self.pending_result = None;
                    self.is_syncing = false;
                    match result {
                        SyncResult::Connected {
                            connection,
                            profiles,
                            version,
                            port_name,
                        } => {
                            self.connection = Some(connection);
                            self.profiles = profiles;
                            self.firmware_version = Some(version);
                            self.selected_port = Some(port_name);
                            self.is_dirty = false;
                        }
                        SyncResult::Reloaded {
                            connection,
                            profiles,
                        } => {
                            self.connection = Some(connection);
                            self.profiles = profiles;
                            self.is_dirty = false;
                        }
                        SyncResult::Saved { connection } => {
                            self.connection = Some(connection);
                            self.is_dirty = false;
                        }
                        SyncResult::Rebooted => {
                            self.disconnect();
                            self.error_message =
                                Some("Device reboot command sent.".to_string());
                        }
                        SyncResult::Error {
                            connection,
                            message,
                        } => {
                            self.connection = connection;
                            self.error_message = Some(message);
                        }
                    }
                }
                Err(mpsc::TryRecvError::Empty) => {
                    // Still waiting — nothing to do.
                }
                Err(mpsc::TryRecvError::Disconnected) => {
                    // Thread dropped sender without sending (e.g. panic).
                    self.pending_result = None;
                    self.is_syncing = false;
                    self.error_message =
                        Some("Operation failed unexpectedly.".to_string());
                }
            }
        }
    }

    fn move_profile_up(&mut self, idx: usize) {
        if idx > 0 && idx < self.profiles.len() {
            self.profiles.swap(idx - 1, idx);
            self.is_dirty = true;
            if let Some(selected) = self.selected_profile_idx {
                if selected == idx {
                    self.selected_profile_idx = Some(idx - 1);
                } else if selected == idx - 1 {
                    self.selected_profile_idx = Some(idx);
                }
            }
        }
    }

    fn move_profile_down(&mut self, idx: usize) {
        if idx < self.profiles.len() - 1 {
            self.profiles.swap(idx, idx + 1);
            self.is_dirty = true;
            if let Some(selected) = self.selected_profile_idx {
                if selected == idx {
                    self.selected_profile_idx = Some(idx + 1);
                } else if selected == idx + 1 {
                    self.selected_profile_idx = Some(idx);
                }
            }
        }
    }

    fn delete_profile(&mut self, idx: usize) {
        if idx < self.profiles.len() {
            self.profiles.remove(idx);
            self.is_dirty = true;
            if let Some(selected) = self.selected_profile_idx {
                if selected == idx {
                    self.selected_profile_idx = None;
                } else if selected > idx {
                    self.selected_profile_idx = Some(selected - 1);
                }
            }
        }
    }

    fn duplicate_profile(&mut self, idx: usize) {
        if idx < self.profiles.len() {
            let mut new_profile = self.profiles[idx];
            // Append "(Copy)" to the profile name
            let current_name = new_profile.name_str();
            let new_name = format!("{} (Copy)", current_name);
            // Truncate to 16 characters max (17 bytes - 1 for null terminator)
            let truncated_name = if new_name.len() > 16 {
                &new_name[..16]
            } else {
                &new_name
            };
            // Clear the name field and set the new name
            new_profile.name = [0; 17];
            for (i, &byte) in truncated_name.as_bytes().iter().enumerate() {
                if i < 16 {
                    new_profile.name[i] = byte;
                }
            }
            // Insert the duplicated profile right after the original
            self.profiles.insert(idx + 1, new_profile);
            self.is_dirty = true;
            if let Some(selected) = self.selected_profile_idx {
                if selected > idx {
                    self.selected_profile_idx = Some(selected + 1);
                }
            }
        }
    }

    fn add_new_profile(&mut self) {
        self.editor_profile = ButtonMapping::default();
        self.editing_profile_idx = None;
        self.show_profile_editor = true;
        self.capturing_field = None;
    }

    fn edit_profile(&mut self, idx: usize) {
        if idx < self.profiles.len() {
            self.editor_profile = self.profiles[idx];
            self.editing_profile_idx = Some(idx);
            self.show_profile_editor = true;
            self.capturing_field = None;
        }
    }

    fn save_edited_profile(&mut self) {
        // Check if name is empty (all zeros or whitespace).
        let name = self.editor_profile.name_str();
        let name_str = name.trim();
        if name_str.is_empty() {
            self.error_message = Some("Profile name cannot be empty".to_string());
            return;
        }

        if let Some(idx) = self.editing_profile_idx {
            if idx < self.profiles.len() {
                self.profiles[idx] = self.editor_profile;
            }
        } else {
            self.profiles.push(self.editor_profile);
        }
        self.is_dirty = true;
        self.show_profile_editor = false;
    }

    fn get_key_combo_mut(&mut self, field_idx: usize) -> Option<&mut KeyCombo> {
        match field_idx {
            FIELD_START_P1 => Some(&mut self.editor_profile.start_p1),
            FIELD_START_P2 => Some(&mut self.editor_profile.start_p2),
            FIELD_COIN_P1 => Some(&mut self.editor_profile.coin_p1),
            FIELD_COIN_P2 => Some(&mut self.editor_profile.coin_p2),
            FIELD_ACTION_P1_1 => Some(&mut self.editor_profile.action_p1_1),
            FIELD_ACTION_P1_2 => Some(&mut self.editor_profile.action_p1_2),
            FIELD_ACTION_P2_1 => Some(&mut self.editor_profile.action_p2_1),
            FIELD_ACTION_P2_2 => Some(&mut self.editor_profile.action_p2_2),
            FIELD_SERVICE_P1 => Some(&mut self.editor_profile.service_p1),
            FIELD_SERVICE_P2 => Some(&mut self.editor_profile.service_p2),
            FIELD_TEST_P1 => Some(&mut self.editor_profile.test_p1),
            FIELD_TEST_P2 => Some(&mut self.editor_profile.test_p2),
            FIELD_PAUSE => Some(&mut self.editor_profile.pause),
            FIELD_SAVE => Some(&mut self.editor_profile.save),
            FIELD_LOAD => Some(&mut self.editor_profile.load),
            FIELD_EXIT => Some(&mut self.editor_profile.exit),
            _ => None,
        }
    }

    fn render_key_binding_row(&mut self, ui: &mut egui::Ui, label: &str, field_idx: usize) {
        ui.horizontal(|ui| {
            ui.add_sized([100.0, 0.0], egui::Label::new(label));
            ui.add_space(10.0);

            // Check if we're capturing this field.
            let is_capturing = self.capturing_field == Some(field_idx);

            // Get current combo display text and capturing state.
            let (button_text, mut ctrl, mut alt, mut shift, mut f_key, mut esc) =
                if let Some(combo) = self.get_key_combo_mut(field_idx) {
                    let text = if is_capturing {
                        "Press a key...".to_string()
                    } else {
                        combo.display()
                    };
                    let ctrl = combo.modifiers & MOD_CTRL != 0;
                    let alt = combo.modifiers & MOD_ALT != 0;
                    let shift = combo.modifiers & MOD_SHIFT != 0;
                    let f_key = combo.modifiers & MOD_F != 0;
                    let esc = combo.modifiers & MOD_ESC != 0;
                    (text, ctrl, alt, shift, f_key, esc)
                } else {
                    return;
                };

            // Key capture button.
            let button = egui::Button::new(button_text).min_size(egui::vec2(150.0, 0.0));
            if ui.add(button).clicked() {
                self.capturing_field = Some(field_idx);
            }

            // Modifier checkboxes.
            ui.checkbox(&mut ctrl, "Ctrl").on_hover_text("Control key");
            ui.checkbox(&mut alt, "Alt").on_hover_text("Alt key");
            ui.checkbox(&mut shift, "Shift").on_hover_text("Shift key");
            ui.checkbox(&mut f_key, "F-Key")
                .on_hover_text("Function key (F1-F24)");
            ui.checkbox(&mut esc, "ESC").on_hover_text("Escape key");

            // Update modifiers based on checkbox values.
            if let Some(combo) = self.get_key_combo_mut(field_idx) {
                combo.modifiers = 0;
                if ctrl {
                    combo.modifiers |= MOD_CTRL;
                }
                if alt {
                    combo.modifiers |= MOD_ALT;
                }
                if shift {
                    combo.modifiers |= MOD_SHIFT;
                }
                if f_key {
                    combo.modifiers |= MOD_F;
                }
                if esc {
                    combo.modifiers |= MOD_ESC;
                }
            }
        });
    }

    fn render_top_panel(&mut self, ctx: &egui::Context) {
        egui::TopBottomPanel::top("top_panel").show(ctx, |ui| {
            ui.horizontal(|ui| {
                // Reload button.
                ui.add_enabled_ui(self.connection.is_some() && !self.is_syncing, |ui| {
                    if ui.button("🔄 Reload").clicked() {
                        if self.is_dirty {
                            self.show_reload_confirmation = true;
                        } else {
                            self.reload_data();
                        }
                    }
                });

                // Save button.
                ui.add_enabled_ui(
                    self.connection.is_some() && self.is_dirty && !self.is_syncing,
                    |ui| {
                        if ui.button("💾 Save Changes").clicked() {
                            self.save_data();
                        }
                    },
                );

                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    // Serial port dropdown.
                    egui::ComboBox::from_label("Serial Port")
                        .selected_text(self.selected_port.as_deref().unwrap_or(
                            if self.serial_ports.is_empty() {
                                "No device found"
                            } else {
                                "Select port..."
                            },
                        ))
                        .show_ui(ui, |ui| {
                            if self.connection.is_some()
                                && ui.selectable_label(false, "Disconnect").clicked()
                            {
                                self.disconnect();
                            }

                            if self.serial_ports.is_empty() {
                                ui.label("No device found");
                            } else {
                                let mut port_to_connect = None;
                                for port in &self.serial_ports {
                                    let label = match &port.port_type {
                                        SerialPortType::UsbPort(info) => {
                                            format!(
                                                "{} ({})",
                                                port.port_name,
                                                info.product.as_deref().unwrap_or("Unknown")
                                            )
                                        }
                                        _ => port.port_name.clone(),
                                    };

                                    if ui.selectable_label(false, label).clicked() {
                                        port_to_connect = Some(port.port_name.clone());
                                    }
                                }
                                if let Some(port_name) = port_to_connect {
                                    self.connect_to_port(port_name);
                                }
                            }
                        });

                    // Refresh button.
                    if ui.button("🔄").on_hover_text("Refresh ports").clicked() {
                        self.refresh_ports();
                    }

                    // Reboot button.
                    ui.add_enabled_ui(self.connection.is_some() && !self.is_syncing, |ui| {
                        if ui.button("🔁 Reboot Device").clicked() {
                            self.reboot_device();
                            self.disconnect();
                        }
                    });
                });
            });
        });
    }

    fn render_status_bar(&mut self, ctx: &egui::Context) {
        egui::TopBottomPanel::bottom("status_bar").show(ctx, |ui| {
            ui.horizontal(|ui| {
                let status_text = if self.connection.is_some() {
                    "Connected"
                } else {
                    "Disconnected"
                };

                ui.label(format!("Status: {}", status_text));

                if let Some(version) = &self.firmware_version {
                    ui.separator();
                    ui.label(format!("Firmware: {}", version));
                }

                if self.is_dirty {
                    ui.separator();
                    let unsaved_color = match self.theme {
                        Theme::Light => egui::Color32::from_rgb(200, 100, 0),
                        _ => egui::Color32::YELLOW,
                    };
                    ui.colored_label(unsaved_color, "● Unsaved changes");
                }

                if let Some(error) = &self.error_message {
                    ui.separator();
                    ui.colored_label(egui::Color32::RED, format!("Error: {}", error));
                }

                // Add space to push theme selector to the right
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    let theme_options = [Theme::System, Theme::Dark, Theme::Light];
                    let theme_labels = ["System", "Dark", "Light"];

                    let mut selected_theme = self.theme;
                    egui::ComboBox::from_label("Theme")
                        .selected_text(self.theme.as_str())
                        .show_ui(ui, |ui| {
                            for (theme, label) in theme_options.iter().zip(theme_labels.iter()) {
                                if ui
                                    .selectable_value(&mut selected_theme, *theme, *label)
                                    .clicked()
                                {
                                    self.theme = selected_theme;
                                    // Save theme to config
                                    let config = ThemeConfig { theme: self.theme };
                                    let _ = confy::store("blast", "config", config);
                                    // Apply the theme
                                    self.apply_theme(ctx);
                                }
                            }
                        });
                });
            });
        });
    }

    pub fn apply_theme(&self, ctx: &egui::Context) {
        let visuals = match self.theme {
            Theme::System => {
                if ctx.style().visuals.dark_mode {
                    egui::Visuals::dark()
                } else {
                    egui::Visuals::light()
                }
            }
            Theme::Dark => egui::Visuals::dark(),
            Theme::Light => egui::Visuals::light(),
        };

        ctx.set_visuals(visuals);
    }

    fn render_profile_list(&mut self, ctx: &egui::Context) {
        egui::CentralPanel::default().show(ctx, |ui| {
            // Disable UI while syncing.
            ui.add_enabled_ui(!self.is_syncing, |ui| {
                // Profile list.
                egui::ScrollArea::vertical().show(ui, |ui| {
                    ui.heading("Profiles");

                    ui.add_space(10.0);

                    if ui
                        .add_enabled(
                            self.connection.is_some(),
                            egui::Button::new("➕ Add New Profile"),
                        )
                        .clicked()
                    {
                        self.add_new_profile();
                    }

                    ui.add_space(10.0);

                    let idx_to_delete = None;
                    let mut idx_to_move_up = None;
                    let mut idx_to_move_down = None;
                    let mut idx_to_edit = None;
                    let mut idx_to_duplicate: Option<usize> = None;

                    for (idx, profile) in self.profiles.iter().enumerate() {
                        ui.group(|ui| {
                            ui.horizontal(|ui| {
                                // Move up button.
                                if ui
                                    .add_enabled(idx > 0, egui::Button::new("⬆"))
                                    .on_hover_text("Move up")
                                    .clicked()
                                {
                                    idx_to_move_up = Some(idx);
                                }

                                // Move down button.
                                if ui
                                    .add_enabled(
                                        idx < self.profiles.len() - 1,
                                        egui::Button::new("⬇"),
                                    )
                                    .on_hover_text("Move down")
                                    .clicked()
                                {
                                    idx_to_move_down = Some(idx);
                                }

                                // Profile name.
                                ui.label(profile.name_str());

                                ui.with_layout(
                                    egui::Layout::right_to_left(egui::Align::Center),
                                    |ui| {
                                        // Delete button.
                                        if ui.button("🗑").on_hover_text("Delete").clicked() {
                                            self.delete_profile_idx = Some(idx);
                                            self.show_delete_confirmation = true;
                                        }

                                        // Duplicate button.
                                        if ui.button("📋").on_hover_text("Duplicate").clicked() {
                                            idx_to_duplicate = Some(idx);
                                        }

                                        // Edit button.
                                        if ui.button("✏").on_hover_text("Edit").clicked() {
                                            idx_to_edit = Some(idx);
                                        }
                                    },
                                );
                            });
                        });
                    }

                    // Handle deferred actions.
                    if let Some(idx) = idx_to_move_up {
                        self.move_profile_up(idx);
                    }
                    if let Some(idx) = idx_to_move_down {
                        self.move_profile_down(idx);
                    }
                    if let Some(idx) = idx_to_edit {
                        self.edit_profile(idx);
                    }
                    if let Some(idx) = idx_to_duplicate {
                        self.duplicate_profile(idx);
                    }
                    if let Some(idx) = idx_to_delete {
                        self.delete_profile(idx);
                    }
                });
            });
        });
    }

    fn render_syncing_overlay(&self, ctx: &egui::Context) {
        if self.is_syncing {
            let screen_rect = ctx.screen_rect();

            // Full-screen semi-transparent backdrop that captures all input.
            egui::Area::new(egui::Id::new("syncing_overlay_backdrop"))
                .order(egui::Order::Foreground)
                .fixed_pos(screen_rect.min)
                .show(ctx, |ui| {
                    let response = ui.allocate_response(
                        screen_rect.size(),
                        egui::Sense::click_and_drag(),
                    );
                    let painter = ui.painter();
                    painter.rect_filled(
                        response.rect,
                        0.0,
                        egui::Color32::from_black_alpha(180),
                    );
                });

            // Centered message and spinner on top.
            egui::Area::new(egui::Id::new("syncing_overlay_content"))
                .order(egui::Order::Foreground)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    egui::Frame::popup(ui.style()).show(ui, |ui| {
                        ui.vertical_centered(|ui| {
                            ui.spinner();
                            ui.add_space(8.0);
                            ui.label(&self.sync_message);
                        });
                    });
                });
        }
    }

    fn render_delete_confirmation(&mut self, ctx: &egui::Context) {
        if self.show_delete_confirmation {
            egui::Window::new("Confirm Delete")
                .collapsible(false)
                .resizable(false)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.label("Are you sure you want to delete this profile?");
                    ui.horizontal(|ui| {
                        if ui.button("Yes").clicked() {
                            if let Some(idx) = self.delete_profile_idx {
                                self.delete_profile(idx);
                            }
                            self.show_delete_confirmation = false;
                            self.delete_profile_idx = None;
                        }
                        if ui.button("Cancel").clicked() {
                            self.show_delete_confirmation = false;
                            self.delete_profile_idx = None;
                        }
                    });
                });
        }
    }

    fn render_reload_confirmation(&mut self, ctx: &egui::Context) {
        if self.show_reload_confirmation {
            egui::Window::new("Confirm Reload")
                .collapsible(false)
                .resizable(false)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.label("You have unsaved changes. Reload anyway?");
                    ui.horizontal(|ui| {
                        if ui.button("Yes").clicked() {
                            self.reload_data();
                            self.show_reload_confirmation = false;
                        }
                        if ui.button("Cancel").clicked() {
                            self.show_reload_confirmation = false;
                        }
                    });
                });
        }
    }

    fn render_profile_editor(&mut self, ctx: &egui::Context) {
        if self.show_profile_editor && !self.is_syncing {
            let title = if self.editing_profile_idx.is_some() {
                "Edit Profile"
            } else {
                "New Profile"
            };

            egui::Window::new(title)
                .collapsible(false)
                .resizable(true)
                .default_width(700.0)
                .default_height(650.0)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    egui::ScrollArea::vertical().show(ui, |ui| {
                        // Profile name input.
                        ui.horizontal(|ui| {
                            ui.label("Profile Name:");
                            let name_str = self.editor_profile.name_str();
                            let mut name_buf = name_str.clone();
                            if ui.text_edit_singleline(&mut name_buf).changed() {
                                // Update name in editor_profile.
                                let bytes = name_buf.as_bytes();
                                let len = bytes.len().min(16);
                                self.editor_profile.name[..len].copy_from_slice(&bytes[..len]);
                                if len < 16 {
                                    self.editor_profile.name[len] = 0;
                                }
                            }
                        });

                        ui.add_space(10.0);
                        ui.separator();
                        ui.add_space(10.0);

                        // Key bindings.
                        ui.heading("Key Bindings");
                        ui.label(
                            "Click a button to capture a key. Press ESC to clear the binding.",
                        );
                        ui.add_space(5.0);

                        self.render_key_binding_row(ui, "Start P1:", FIELD_START_P1);
                        self.render_key_binding_row(ui, "Start P2:", FIELD_START_P2);
                        ui.add_space(5.0);

                        self.render_key_binding_row(ui, "Coin P1:", FIELD_COIN_P1);
                        self.render_key_binding_row(ui, "Coin P2:", FIELD_COIN_P2);
                        ui.add_space(5.0);

                        self.render_key_binding_row(ui, "Action P1.1:", FIELD_ACTION_P1_1);
                        self.render_key_binding_row(ui, "Action P1.2:", FIELD_ACTION_P1_2);
                        self.render_key_binding_row(ui, "Action P2.1:", FIELD_ACTION_P2_1);
                        self.render_key_binding_row(ui, "Action P2.2:", FIELD_ACTION_P2_2);
                        ui.add_space(5.0);

                        self.render_key_binding_row(ui, "Service P1:", FIELD_SERVICE_P1);
                        self.render_key_binding_row(ui, "Service P2:", FIELD_SERVICE_P2);
                        ui.add_space(5.0);

                        self.render_key_binding_row(ui, "Test P1:", FIELD_TEST_P1);
                        self.render_key_binding_row(ui, "Test P2:", FIELD_TEST_P2);
                        ui.add_space(5.0);

                        self.render_key_binding_row(ui, "Pause:", FIELD_PAUSE);
                        self.render_key_binding_row(ui, "Save:", FIELD_SAVE);
                        self.render_key_binding_row(ui, "Load:", FIELD_LOAD);
                        self.render_key_binding_row(ui, "Exit:", FIELD_EXIT);

                        ui.add_space(10.0);
                        ui.separator();
                        ui.add_space(10.0);

                        // Action buttons.
                        ui.horizontal(|ui| {
                            let button_text = if self.editing_profile_idx.is_some() {
                                "Save"
                            } else {
                                "Add"
                            };

                            let name = self.editor_profile.name_str();
                            let name_str = name.trim();
                            let can_save = !name_str.is_empty();

                            if ui
                                .add_enabled(can_save, egui::Button::new(button_text))
                                .clicked()
                            {
                                self.save_edited_profile();
                            }

                            if ui.button("Cancel").clicked() {
                                self.show_profile_editor = false;
                                self.capturing_field = None;
                            }
                        });
                    });
                });
        }
    }

    fn handle_key_capture(&mut self, ctx: &egui::Context) {
        if let Some(field_idx) = self.capturing_field {
            ctx.input(|i| {
                if !i.events.is_empty() {
                    for event in &i.events {
                        if let egui::Event::Key {
                            key, pressed: true, ..
                        } = event
                        {
                            if let Some(combo) = self.get_key_combo_mut(field_idx) {
                                // Map egui key to ASCII/keycode.
                                use egui::Key;
                                combo.key = match key {
                                    Key::Escape => {
                                        // ESC clears the binding.
                                        *combo = KeyCombo {
                                            modifiers: 0,
                                            key: 0,
                                        };
                                        self.capturing_field = None;
                                        break;
                                    }
                                    Key::A => b'a',
                                    Key::B => b'b',
                                    Key::C => b'c',
                                    Key::D => b'd',
                                    Key::E => b'e',
                                    Key::F => b'f',
                                    Key::G => b'g',
                                    Key::H => b'h',
                                    Key::I => b'i',
                                    Key::J => b'j',
                                    Key::K => b'k',
                                    Key::L => b'l',
                                    Key::M => b'm',
                                    Key::N => b'n',
                                    Key::O => b'o',
                                    Key::P => b'p',
                                    Key::Q => b'q',
                                    Key::R => b'r',
                                    Key::S => b's',
                                    Key::T => b't',
                                    Key::U => b'u',
                                    Key::V => b'v',
                                    Key::W => b'w',
                                    Key::X => b'x',
                                    Key::Y => b'y',
                                    Key::Z => b'z',
                                    Key::Num0 => b'0',
                                    Key::Num1 => b'1',
                                    Key::Num2 => b'2',
                                    Key::Num3 => b'3',
                                    Key::Num4 => b'4',
                                    Key::Num5 => b'5',
                                    Key::Num6 => b'6',
                                    Key::Num7 => b'7',
                                    Key::Num8 => b'8',
                                    Key::Num9 => b'9',
                                    Key::Space => b' ',
                                    Key::Enter => 13,
                                    Key::F1 => {
                                        combo.modifiers |= MOD_F;
                                        1
                                    }
                                    Key::F2 => {
                                        combo.modifiers |= MOD_F;
                                        2
                                    }
                                    Key::F3 => {
                                        combo.modifiers |= MOD_F;
                                        3
                                    }
                                    Key::F4 => {
                                        combo.modifiers |= MOD_F;
                                        4
                                    }
                                    Key::F5 => {
                                        combo.modifiers |= MOD_F;
                                        5
                                    }
                                    Key::F6 => {
                                        combo.modifiers |= MOD_F;
                                        6
                                    }
                                    Key::F7 => {
                                        combo.modifiers |= MOD_F;
                                        7
                                    }
                                    Key::F8 => {
                                        combo.modifiers |= MOD_F;
                                        8
                                    }
                                    Key::F9 => {
                                        combo.modifiers |= MOD_F;
                                        9
                                    }
                                    Key::F10 => {
                                        combo.modifiers |= MOD_F;
                                        10
                                    }
                                    Key::F11 => {
                                        combo.modifiers |= MOD_F;
                                        11
                                    }
                                    Key::F12 => {
                                        combo.modifiers |= MOD_F;
                                        12
                                    }
                                    _ => combo.key, // Keep existing key if not mapped.
                                };
                            }
                            self.capturing_field = None;
                            break;
                        }
                    }
                }
            });
        }
    }
}

impl eframe::App for BlastApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.poll_pending_result();

        self.render_top_panel(ctx);
        self.render_status_bar(ctx);
        self.render_profile_list(ctx);
        self.render_syncing_overlay(ctx);
        self.render_delete_confirmation(ctx);
        self.render_reload_confirmation(ctx);
        self.render_profile_editor(ctx);
        self.handle_key_capture(ctx);

        // Keep repainting while waiting for background operation.
        if self.is_syncing {
            ctx.request_repaint();
        }
    }
}
