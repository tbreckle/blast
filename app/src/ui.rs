use crate::keymap::{apply_captured_key, CaptureResult};
use crate::protocol::FirmwareConnection;
use crate::types::{
    ButtonMapping, FirmwareVersion, KeyCombo, MOD_ALT, MOD_CTRL, MOD_ESC, MOD_F, MOD_SHIFT,
};
use anyhow::Result;
use serde::{Deserialize, Serialize};
use serialport::{SerialPortInfo, SerialPortType};
use slint::{ComponentHandle, Model, ModelRc, SharedString, Timer, TimerMode, VecModel};
use std::cell::RefCell;
use std::rc::Rc;
use std::sync::mpsc;
use std::thread;
use std::time::Duration;

slint::include_modules!();

#[derive(Debug, Default, Clone, Copy, Serialize, Deserialize, PartialEq)]
pub enum Theme {
    #[default]
    System,
    Dark,
    Light,
}

impl Theme {
    /// Index in the theme ComboBox (see `theme` in app.slint).
    fn index(self) -> i32 {
        match self {
            Theme::System => 0,
            Theme::Dark => 1,
            Theme::Light => 2,
        }
    }

    fn from_index(index: i32) -> Self {
        match index {
            1 => Theme::Dark,
            2 => Theme::Light,
            _ => Theme::System,
        }
    }
}

#[derive(Debug, Default, Serialize, Deserialize)]
struct ThemeConfig {
    theme: Theme,
}

/// Key binding rows of the profile editor: label and whether it starts a new group.
const BINDING_ROWS: [(&str, bool); 16] = [
    ("Start P1:", true),
    ("Start P2:", false),
    ("Coin P1:", true),
    ("Coin P2:", false),
    ("Action P1.1:", true),
    ("Action P1.2:", false),
    ("Action P2.1:", false),
    ("Action P2.2:", false),
    ("Service P1:", true),
    ("Service P2:", false),
    ("Test P1:", true),
    ("Test P2:", false),
    ("Pause:", true),
    ("Save:", false),
    ("Load:", false),
    ("Exit:", false),
];

/// Key combo of a profile for a row index of BINDING_ROWS.
fn binding_mut(profile: &mut ButtonMapping, index: usize) -> Option<&mut KeyCombo> {
    match index {
        0 => Some(&mut profile.start_p1),
        1 => Some(&mut profile.start_p2),
        2 => Some(&mut profile.coin_p1),
        3 => Some(&mut profile.coin_p2),
        4 => Some(&mut profile.action_p1_1),
        5 => Some(&mut profile.action_p1_2),
        6 => Some(&mut profile.action_p2_1),
        7 => Some(&mut profile.action_p2_2),
        8 => Some(&mut profile.service_p1),
        9 => Some(&mut profile.service_p2),
        10 => Some(&mut profile.test_p1),
        11 => Some(&mut profile.test_p2),
        12 => Some(&mut profile.pause),
        13 => Some(&mut profile.save),
        14 => Some(&mut profile.load),
        15 => Some(&mut profile.exit),
        _ => None,
    }
}

/// Modifier flag for a checkbox index (see the Modifier global in app.slint).
fn modifier_flag(index: i32) -> Option<u8> {
    match index {
        0 => Some(MOD_CTRL),
        1 => Some(MOD_ALT),
        2 => Some(MOD_SHIFT),
        3 => Some(MOD_F),
        4 => Some(MOD_ESC),
        _ => None,
    }
}

/// Maximum time to wait for a running background operation when the app closes.
const EXIT_SYNC_TIMEOUT: Duration = Duration::from_secs(10);

/// Interval for checking whether a background operation has finished.
const POLL_INTERVAL: Duration = Duration::from_millis(50);

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

/// Profile editor state (only present while the editor is open).
struct EditorState {
    profile: ButtonMapping,
    editing_idx: Option<usize>,
    capturing_field: Option<usize>,
    error: Option<String>,
}

#[derive(Default)]
struct AppState {
    // Connection state.
    serial_ports: Vec<SerialPortInfo>,
    connected_port: Option<String>,
    connection: Option<FirmwareConnection>,
    firmware_version: Option<FirmwareVersion>,

    // Profile data.
    profiles: Vec<ButtonMapping>,
    is_dirty: bool,

    // Background operation: status message and result channel.
    busy_message: Option<String>,
    pending_result: Option<mpsc::Receiver<SyncResult>>,

    // Dialogs.
    delete_profile_idx: Option<usize>,
    show_reload_confirmation: bool,
    editor: Option<EditorState>,

    // Status bar message and whether it is an error.
    message: Option<(String, bool)>,

    theme: Theme,
}

impl AppState {
    fn set_error(&mut self, message: impl Into<String>) {
        self.message = Some((message.into(), true));
    }

    fn set_info(&mut self, message: impl Into<String>) {
        self.message = Some((message.into(), false));
    }

    fn disconnect(&mut self) {
        // Switch firmware back to BLAST protocol before dropping the connection.
        if let Some(conn) = self.connection.as_mut() {
            if let Err(e) = conn.switch_to_blast() {
                eprintln!("Warning: Failed to switch back to BLAST mode: {}", e);
            }
        }
        self.connection = None;
        self.connected_port = None;
        self.firmware_version = None;
        self.profiles.clear();
        self.is_dirty = false;
        self.editor = None;
        self.delete_profile_idx = None;
        self.show_reload_confirmation = false;
    }

    fn apply_sync_result(&mut self, result: SyncResult) {
        self.pending_result = None;
        self.busy_message = None;
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
                self.connected_port = Some(port_name);
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
                self.set_info("Profiles saved.");
            }
            SyncResult::Rebooted => {
                self.disconnect();
                self.set_info("Device reboot command sent.");
            }
            SyncResult::Error {
                connection,
                message,
            } => {
                self.connection = connection;
                if self.connection.is_none() {
                    self.connected_port = None;
                }
                self.set_error(message);
            }
        }
    }

    fn move_profile_up(&mut self, idx: usize) {
        if idx > 0 && idx < self.profiles.len() {
            self.profiles.swap(idx - 1, idx);
            self.is_dirty = true;
        }
    }

    fn move_profile_down(&mut self, idx: usize) {
        if idx + 1 < self.profiles.len() {
            self.profiles.swap(idx, idx + 1);
            self.is_dirty = true;
        }
    }

    fn delete_profile(&mut self, idx: usize) {
        if idx < self.profiles.len() {
            self.profiles.remove(idx);
            self.is_dirty = true;
        }
    }

    fn duplicate_profile(&mut self, idx: usize) {
        if let Some(profile) = self.profiles.get(idx) {
            let mut new_profile = *profile;
            new_profile.set_name(&format!("{} (Copy)", profile.name_str()));
            // Game names must be unique, so the copy starts without one.
            new_profile.set_game_name("");
            // Insert the duplicated profile right after the original.
            self.profiles.insert(idx + 1, new_profile);
            self.is_dirty = true;
        }
    }

    /// Validate and store the edited profile. Returns false if the editor stays open.
    fn save_edited_profile(&mut self) -> bool {
        let Some(editor) = self.editor.as_mut() else {
            return false;
        };

        if editor.profile.name_str().trim().is_empty() {
            editor.error = Some("Profile name cannot be empty.".to_string());
            return false;
        }

        // Game names must be unique, otherwise only the first matching profile is reachable.
        let game_name = editor.profile.game_name_str();
        if !game_name.is_empty() {
            let duplicate = self.profiles.iter().enumerate().find(|(idx, profile)| {
                Some(*idx) != editor.editing_idx && profile.game_name_str() == game_name
            });
            if let Some((_, profile)) = duplicate {
                editor.error = Some(format!(
                    "Game name \"{}\" is already used by profile \"{}\".",
                    game_name,
                    profile.name_str()
                ));
                return false;
            }
        }

        let profile = editor.profile;
        match editor.editing_idx {
            Some(idx) if idx < self.profiles.len() => self.profiles[idx] = profile,
            Some(_) => {}
            None => self.profiles.push(profile),
        }
        self.is_dirty = true;
        self.editor = None;
        true
    }
}

/// USB product name and VID/PID reported by the firmware (see USB.setProduct/setVIDPID in
/// firmware.ino).
const BLAST_PRODUCT_NAME: &str = "B.L.A.S.T.";
const BLAST_USB_ID: (u16, u16) = (0xF144, 0x0001);

/// Index of the port to preselect: the first USB device named "B.L.A.S.T.", otherwise the
/// first one with the B.L.A.S.T. VID/PID (Windows may report a generic driver name instead).
fn blast_port_index(ports: &[SerialPortInfo]) -> Option<usize> {
    let usb_info = |port: &SerialPortInfo| match &port.port_type {
        SerialPortType::UsbPort(info) => Some(info.clone()),
        _ => None,
    };
    ports
        .iter()
        .position(|port| {
            usb_info(port)
                .and_then(|info| info.product)
                .is_some_and(|product| product.contains(BLAST_PRODUCT_NAME))
        })
        .or_else(|| {
            ports.iter().position(|port| {
                usb_info(port).is_some_and(|info| (info.vid, info.pid) == BLAST_USB_ID)
            })
        })
}

/// Label for a serial port in the port ComboBox. USB ports end in `[VID:PID]`, because Windows
/// reports every CDC device as "USB Serial Device" and the ID is the only way to tell them apart.
fn port_label(port: &SerialPortInfo) -> String {
    match &port.port_type {
        SerialPortType::UsbPort(info) => {
            let id = format!("[{:04X}:{:04X}]", info.vid, info.pid);
            match info.product.as_deref().or(info.manufacturer.as_deref()) {
                Some(name) => format!("{} ({}) {}", port.port_name, name, id),
                None => format!("{} {}", port.port_name, id),
            }
        }
        _ => port.port_name.clone(),
    }
}

/// Update a model in place: rows are only replaced when they changed, so the UI keeps focus
/// and state of unchanged rows.
fn sync_model<T: Clone + PartialEq + 'static>(model: &VecModel<T>, items: Vec<T>) {
    if model.row_count() == items.len() {
        for (i, item) in items.into_iter().enumerate() {
            if model.row_data(i).as_ref() != Some(&item) {
                model.set_row_data(i, item);
            }
        }
    } else {
        model.set_vec(items);
    }
}

/// Connects the Slint UI to the application state.
struct Controller {
    ui: slint::Weak<AppWindow>,
    state: RefCell<AppState>,
    port_model: Rc<VecModel<SharedString>>,
    profile_model: Rc<VecModel<ProfileRow>>,
    binding_model: Rc<VecModel<BindingRow>>,
    poll_timer: Timer,
}

impl Controller {
    /// Push the application state to the UI.
    fn refresh(&self) {
        let Some(ui) = self.ui.upgrade() else {
            return;
        };
        let st = self.state.borrow();

        // Connection.
        let labels: Vec<SharedString> = if st.serial_ports.is_empty() {
            vec!["No device found".into()]
        } else {
            st.serial_ports
                .iter()
                .map(|port| port_label(port).into())
                .collect()
        };
        sync_model(&self.port_model, labels);
        ui.set_has_ports(!st.serial_ports.is_empty());
        if ui.get_port_index() as usize >= self.port_model.row_count() {
            ui.set_port_index(0);
        }
        ui.set_connected(st.connected_port.is_some());
        ui.set_connected_port(st.connected_port.clone().unwrap_or_default().into());
        ui.set_firmware_version(
            st.firmware_version
                .as_ref()
                .map(|v| v.to_string())
                .unwrap_or_default()
                .into(),
        );

        // Profiles.
        let rows = st
            .profiles
            .iter()
            .map(|p| ProfileRow {
                name: p.name_str().into(),
                game_name: p.game_name_str().into(),
            })
            .collect();
        sync_model(&self.profile_model, rows);
        ui.set_dirty(st.is_dirty);

        // Status and background operation.
        let (message, is_error) = st.message.clone().unwrap_or_default();
        ui.set_message(message.into());
        ui.set_message_is_error(is_error);
        ui.set_busy(st.busy_message.is_some());
        ui.set_busy_message(st.busy_message.clone().unwrap_or_default().into());

        // Dialogs.
        ui.set_show_delete_confirmation(st.delete_profile_idx.is_some());
        ui.set_show_reload_confirmation(st.show_reload_confirmation);

        // Profile editor.
        ui.set_editor_visible(st.editor.is_some());
        if let Some(editor) = &st.editor {
            let is_new = editor.editing_idx.is_none();
            ui.set_editor_title(
                if is_new {
                    "New Profile"
                } else {
                    "Edit Profile"
                }
                .into(),
            );
            ui.set_editor_confirm_label(if is_new { "Add" } else { "Save" }.into());
            ui.set_editor_can_save(!editor.profile.name_str().trim().is_empty());
            ui.set_editor_error(editor.error.clone().unwrap_or_default().into());
            ui.set_capturing_field(editor.capturing_field.map_or(-1, |i| i as i32));

            let mut profile = editor.profile;
            let rows = BINDING_ROWS
                .iter()
                .enumerate()
                .filter_map(|(i, (label, group_start))| {
                    let combo = *binding_mut(&mut profile, i)?;
                    Some(BindingRow {
                        label: (*label).into(),
                        key_text: combo.display().into(),
                        ctrl: combo.modifiers & MOD_CTRL != 0,
                        alt: combo.modifiers & MOD_ALT != 0,
                        shift: combo.modifiers & MOD_SHIFT != 0,
                        fkey: combo.modifiers & MOD_F != 0,
                        esc: combo.modifiers & MOD_ESC != 0,
                        group_start: *group_start,
                    })
                })
                .collect();
            sync_model(&self.binding_model, rows);
        }
    }

    /// Run `job` on a background thread while the busy overlay is shown.
    fn start_operation(
        self: &Rc<Self>,
        message: &str,
        job: impl FnOnce() -> SyncResult + Send + 'static,
    ) {
        let (tx, rx) = mpsc::channel();
        {
            let mut st = self.state.borrow_mut();
            st.busy_message = Some(message.to_string());
            st.message = None;
            st.pending_result = Some(rx);
        }
        thread::spawn(move || {
            let _ = tx.send(job());
        });

        let weak = Rc::downgrade(self);
        self.poll_timer
            .start(TimerMode::Repeated, POLL_INTERVAL, move || {
                if let Some(controller) = weak.upgrade() {
                    controller.poll_pending_result();
                }
            });
        self.refresh();
    }

    fn poll_pending_result(&self) {
        let received = {
            let st = self.state.borrow();
            let Some(rx) = st.pending_result.as_ref() else {
                self.poll_timer.stop();
                return;
            };
            match rx.try_recv() {
                Ok(result) => Some(Ok(result)),
                Err(mpsc::TryRecvError::Empty) => None,
                Err(mpsc::TryRecvError::Disconnected) => Some(Err(())),
            }
        };

        let Some(received) = received else {
            return;
        };
        self.poll_timer.stop();
        {
            let mut st = self.state.borrow_mut();
            match received {
                Ok(result) => st.apply_sync_result(result),
                Err(()) => {
                    // Thread dropped sender without sending (e.g. panic).
                    st.pending_result = None;
                    st.busy_message = None;
                    st.set_error("Operation failed unexpectedly.");
                }
            }
        }
        self.refresh();
    }

    fn refresh_ports(&self) {
        self.state.borrow_mut().serial_ports = serialport::available_ports().unwrap_or_default();
        self.refresh();
        self.select_blast_port();
    }

    /// Preselect a connected B.L.A.S.T. controller in the port ComboBox.
    fn select_blast_port(&self) {
        let index = blast_port_index(&self.state.borrow().serial_ports);
        if let (Some(index), Some(ui)) = (index, self.ui.upgrade()) {
            ui.set_port_index(index as i32);
        }
    }

    fn connect(self: &Rc<Self>, port_index: usize) {
        let port_name = {
            let st = self.state.borrow();
            match st.serial_ports.get(port_index) {
                Some(port) => port.port_name.clone(),
                None => return,
            }
        };

        self.start_operation("Connecting to device...", move || {
            let result = (|| -> Result<(FirmwareConnection, Vec<ButtonMapping>, FirmwareVersion), String> {
                let port = serialport::new(&port_name, 115200)
                    .timeout(Duration::from_millis(100))
                    // The Arduino-Pico core drops all USB serial output while DTR is low.
                    // Linux raises DTR on open anyway, but on Windows serialport clears it.
                    .dtr_on_open(true)
                    .open()
                    .map_err(|e| format!("Failed to open port: {}", e))?;

                // Flush stale data and give device time to stabilize after port open.
                let _ = port.clear(serialport::ClearBuffer::All);
                thread::sleep(Duration::from_millis(500));
                let _ = port.clear(serialport::ClearBuffer::Input);

                let mut conn = FirmwareConnection::new(port);

                conn.enable_slip_mode()
                    .map_err(|e| format!("Failed to switch to SLIP mode: {}", e))?;

                let version = conn.get_version()
                    .map_err(|e| format!("Failed to read firmware version: {}", e))?;

                let profiles = conn.get_all_profiles()
                    .map_err(|e| format!("Connection failed: Not a valid Blast device or communication error: {}", e))?;

                Ok((conn, profiles, version))
            })();

            match result {
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
            }
        });
    }

    fn disconnect(&self) {
        self.state.borrow_mut().disconnect();
        self.refresh();
    }

    fn reload(self: &Rc<Self>) {
        let Some(mut conn) = self.state.borrow_mut().connection.take() else {
            return;
        };
        self.start_operation("Reloading data...", move || match conn.get_all_profiles() {
            Ok(profiles) => SyncResult::Reloaded {
                connection: conn,
                profiles,
            },
            Err(e) => SyncResult::Error {
                connection: Some(conn),
                message: format!("Failed to reload profiles: {}", e),
            },
        });
    }

    fn request_reload(self: &Rc<Self>) {
        let is_dirty = self.state.borrow().is_dirty;
        if is_dirty {
            self.state.borrow_mut().show_reload_confirmation = true;
            self.refresh();
        } else {
            self.reload();
        }
    }

    fn save(self: &Rc<Self>) {
        let (conn, profiles) = {
            let mut st = self.state.borrow_mut();
            let Some(conn) = st.connection.take() else {
                return;
            };
            (conn, st.profiles.clone())
        };

        self.start_operation("Saving profiles...", move || {
            let mut conn = conn;
            let result = (|| -> Result<(), String> {
                let max_profiles = conn
                    .get_max_profiles()
                    .map_err(|e| format!("Failed to query max profiles: {}", e))?
                    as usize;

                if profiles.len() > max_profiles {
                    return Err(format!(
                        "Too many profiles: {} exceeds firmware limit of {}.",
                        profiles.len(),
                        max_profiles
                    ));
                }

                for (idx, profile) in profiles.iter().enumerate() {
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

            match result {
                Ok(()) => SyncResult::Saved { connection: conn },
                Err(message) => SyncResult::Error {
                    connection: Some(conn),
                    message,
                },
            }
        });
    }

    fn reboot(self: &Rc<Self>) {
        let Some(mut conn) = self.state.borrow_mut().connection.take() else {
            return;
        };
        self.start_operation("Rebooting device...", move || match conn.reboot_flash() {
            Ok(_) => SyncResult::Rebooted,
            Err(e) => SyncResult::Error {
                connection: Some(conn),
                message: format!("Failed to reboot device: {}", e),
            },
        });
    }

    fn theme_selected(&self, index: i32) {
        let theme = Theme::from_index(index);
        self.state.borrow_mut().theme = theme;
        if let Err(e) = confy::store("blast", "config", ThemeConfig { theme }) {
            eprintln!("Warning: Failed to store theme: {}", e);
        }
    }

    /// Apply a change to the state and refresh the UI.
    fn update(&self, change: impl FnOnce(&mut AppState)) {
        change(&mut self.state.borrow_mut());
        self.refresh();
    }

    fn open_editor(&self, editing_idx: Option<usize>) {
        let profile = {
            let mut st = self.state.borrow_mut();
            let profile = match editing_idx {
                Some(idx) => match st.profiles.get(idx) {
                    Some(profile) => *profile,
                    None => return,
                },
                None => ButtonMapping::default(),
            };
            st.editor = Some(EditorState {
                profile,
                editing_idx,
                capturing_field: None,
                error: None,
            });
            profile
        };

        if let Some(ui) = self.ui.upgrade() {
            ui.set_editor_name(profile.name_str().into());
            ui.set_editor_game_name(profile.game_name_str().into());
        }
        self.refresh();
    }

    /// Store an edited text field and write the (possibly truncated) value back to the UI.
    fn editor_text_edited(
        &self,
        text: &str,
        store: impl FnOnce(&mut ButtonMapping, &str) -> String,
        write_back: impl FnOnce(&AppWindow, SharedString),
    ) {
        let stored = {
            let mut st = self.state.borrow_mut();
            let Some(editor) = st.editor.as_mut() else {
                return;
            };
            editor.error = None;
            store(&mut editor.profile, text)
        };
        if stored != text {
            if let Some(ui) = self.ui.upgrade() {
                write_back(&ui, stored.into());
            }
        }
        self.refresh();
    }

    fn key_captured(&self, text: &str) {
        self.update(|st| {
            if let Some(editor) = st.editor.as_mut() {
                if let Some(field) = editor.capturing_field {
                    if let Some(combo) = binding_mut(&mut editor.profile, field) {
                        if apply_captured_key(combo, text) == CaptureResult::Done {
                            editor.capturing_field = None;
                        }
                    }
                }
            }
        });
    }

    fn modifier_toggled(&self, field: usize, modifier: i32, checked: bool) {
        self.update(|st| {
            let Some(editor) = st.editor.as_mut() else {
                return;
            };
            let (Some(combo), Some(flag)) = (
                binding_mut(&mut editor.profile, field),
                modifier_flag(modifier),
            ) else {
                return;
            };
            if checked {
                combo.modifiers |= flag;
            } else {
                combo.modifiers &= !flag;
            }
        });
    }

    /// Wait for a running background operation, then disconnect. Called after the window closed.
    fn shutdown(&self) {
        self.poll_timer.stop();
        let mut st = self.state.borrow_mut();
        // A running background operation owns the connection; wait for it to hand it back.
        if let Some(rx) = st.pending_result.take() {
            match rx.recv_timeout(EXIT_SYNC_TIMEOUT) {
                Ok(result) => st.apply_sync_result(result),
                Err(e) => eprintln!(
                    "Warning: Background operation did not finish on exit: {}",
                    e
                ),
            }
        }

        // Switch the firmware back to BLAST mode and close the serial port.
        st.disconnect();
    }
}

/// Create the main window, run the event loop and disconnect cleanly afterwards.
pub fn run() -> Result<()> {
    let ui = AppWindow::new()?;

    let theme = confy::load::<ThemeConfig>("blast", "config")
        .map(|config| config.theme)
        .unwrap_or_default();

    let controller = Rc::new(Controller {
        ui: ui.as_weak(),
        state: RefCell::new(AppState {
            serial_ports: serialport::available_ports().unwrap_or_default(),
            theme,
            ..Default::default()
        }),
        port_model: Rc::new(VecModel::default()),
        profile_model: Rc::new(VecModel::default()),
        binding_model: Rc::new(VecModel::default()),
        poll_timer: Timer::default(),
    });

    ui.set_app_version(env!("CARGO_PKG_VERSION").into());
    ui.set_theme(theme.index());
    ui.set_port_labels(ModelRc::from(controller.port_model.clone()));
    ui.set_profiles(ModelRc::from(controller.profile_model.clone()));
    ui.set_bindings(ModelRc::from(controller.binding_model.clone()));

    // Connection.
    let c = controller.clone();
    ui.on_refresh_ports(move || c.refresh_ports());
    let c = controller.clone();
    ui.on_connect(move |index| {
        if let Ok(index) = usize::try_from(index) {
            c.connect(index);
        }
    });
    let c = controller.clone();
    ui.on_disconnect(move || c.disconnect());
    let c = controller.clone();
    ui.on_reload(move || c.request_reload());
    let c = controller.clone();
    ui.on_save(move || c.save());
    let c = controller.clone();
    ui.on_reboot(move || c.reboot());
    let c = controller.clone();
    ui.on_theme_selected(move |index| c.theme_selected(index));

    // Profile list. Indices come from the UI and are validated by the AppState methods.
    let c = controller.clone();
    ui.on_add_profile(move || c.open_editor(None));
    let c = controller.clone();
    ui.on_edit_profile(move |i| c.open_editor(Some(i as usize)));
    let c = controller.clone();
    ui.on_duplicate_profile(move |i| c.update(|st| st.duplicate_profile(i as usize)));
    let c = controller.clone();
    ui.on_move_profile_up(move |i| c.update(|st| st.move_profile_up(i as usize)));
    let c = controller.clone();
    ui.on_move_profile_down(move |i| c.update(|st| st.move_profile_down(i as usize)));
    let c = controller.clone();
    ui.on_delete_profile(move |i| c.update(|st| st.delete_profile_idx = Some(i as usize)));
    let c = controller.clone();
    ui.on_confirm_delete(move || {
        c.update(|st| {
            if let Some(idx) = st.delete_profile_idx.take() {
                st.delete_profile(idx);
            }
        })
    });
    let c = controller.clone();
    ui.on_cancel_delete(move || c.update(|st| st.delete_profile_idx = None));
    let c = controller.clone();
    ui.on_confirm_reload(move || {
        c.state.borrow_mut().show_reload_confirmation = false;
        c.reload();
    });
    let c = controller.clone();
    ui.on_cancel_reload(move || c.update(|st| st.show_reload_confirmation = false));

    // Profile editor.
    let c = controller.clone();
    ui.on_editor_name_edited(move |text| {
        c.editor_text_edited(
            &text,
            |profile, text| {
                profile.set_name(text);
                profile.name_str()
            },
            |ui, value| ui.set_editor_name(value),
        )
    });
    let c = controller.clone();
    ui.on_editor_game_name_edited(move |text| {
        c.editor_text_edited(
            &text,
            |profile, text| {
                profile.set_game_name(text);
                profile.game_name_str()
            },
            |ui, value| ui.set_editor_game_name(value),
        )
    });
    let c = controller.clone();
    ui.on_start_capture(move |field| {
        c.update(|st| {
            if let Some(editor) = st.editor.as_mut() {
                editor.capturing_field = usize::try_from(field).ok();
            }
        })
    });
    let c = controller.clone();
    ui.on_key_captured(move |text| c.key_captured(&text));
    let c = controller.clone();
    ui.on_modifier_toggled(move |field, modifier, checked| {
        if let Ok(field) = usize::try_from(field) {
            c.modifier_toggled(field, modifier, checked);
        }
    });
    let c = controller.clone();
    ui.on_editor_save(move || {
        c.update(|st| {
            st.save_edited_profile();
        })
    });
    let c = controller.clone();
    ui.on_editor_cancel(move || c.update(|st| st.editor = None));

    controller.refresh();
    controller.select_blast_port();
    ui.run()?;

    controller.shutdown();
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn profile(name: &str, game_name: &str) -> ButtonMapping {
        let mut profile = ButtonMapping::default();
        profile.set_name(name);
        profile.set_game_name(game_name);
        profile
    }

    fn names(st: &AppState) -> Vec<String> {
        st.profiles.iter().map(|p| p.name_str()).collect()
    }

    fn state_with(profiles: Vec<ButtonMapping>) -> AppState {
        AppState {
            profiles,
            ..Default::default()
        }
    }

    fn port(name: &str, usb: Option<(u16, u16, Option<&str>)>) -> SerialPortInfo {
        SerialPortInfo {
            port_name: name.to_string(),
            port_type: match usb {
                Some((vid, pid, product)) => SerialPortType::UsbPort(serialport::UsbPortInfo {
                    vid,
                    pid,
                    serial_number: None,
                    manufacturer: None,
                    product: product.map(str::to_string),
                }),
                None => SerialPortType::Unknown,
            },
        }
    }

    #[test]
    fn blast_port_is_found_by_name_then_usb_id() {
        let other = port(
            "/dev/ttyACM0",
            Some((0x041E, 0x3278, Some("Sound Blaster X4"))),
        );
        let by_name = port("/dev/ttyACM1", Some((0x1234, 0x5678, Some("B.L.A.S.T."))));
        let by_id = port("COM3", Some((0xF144, 0x0001, Some("USB Serial Device"))));
        let plain = port("/dev/ttyS0", None);

        assert_eq!(blast_port_index(&[]), None);
        assert_eq!(blast_port_index(&[other.clone(), plain.clone()]), None);
        assert_eq!(
            blast_port_index(&[other.clone(), by_id.clone(), by_name.clone()]),
            Some(2)
        );
        assert_eq!(blast_port_index(&[plain, other, by_id]), Some(2));
    }

    #[test]
    fn port_label_ends_with_usb_id() {
        assert_eq!(
            port_label(&port(
                "COM3",
                Some((0xF144, 0x0001, Some("USB Serial Device")))
            )),
            "COM3 (USB Serial Device) [F144:0001]"
        );
        assert_eq!(
            port_label(&port("COM4", Some((0x041E, 0x3278, None)))),
            "COM4 [041E:3278]"
        );
        assert_eq!(port_label(&port("/dev/ttyS0", None)), "/dev/ttyS0");
    }

    #[test]
    fn move_profiles() {
        let mut st = state_with(vec![profile("A", ""), profile("B", ""), profile("C", "")]);
        st.move_profile_up(0);
        st.move_profile_down(2);
        assert_eq!(names(&st), ["A", "B", "C"]);
        assert!(!st.is_dirty);

        st.move_profile_down(0);
        assert_eq!(names(&st), ["B", "A", "C"]);
        st.move_profile_up(2);
        assert_eq!(names(&st), ["B", "C", "A"]);
        assert!(st.is_dirty);
    }

    #[test]
    fn duplicate_profile_clears_game_name() {
        let mut st = state_with(vec![profile("Time Crisis", "tcrisis"), profile("B", "")]);
        st.duplicate_profile(0);
        assert_eq!(names(&st), ["Time Crisis", "Time Crisis (Cop", "B"]);
        assert_eq!(st.profiles[1].game_name_str(), "");
        assert!(st.is_dirty);
    }

    #[test]
    fn delete_profile_ignores_invalid_index() {
        let mut st = state_with(vec![profile("A", ""), profile("B", "")]);
        st.delete_profile(5);
        assert!(!st.is_dirty);
        st.delete_profile(0);
        assert_eq!(names(&st), ["B"]);
        assert!(st.is_dirty);
    }

    fn open_editor(st: &mut AppState, profile: ButtonMapping, editing_idx: Option<usize>) {
        st.editor = Some(EditorState {
            profile,
            editing_idx,
            capturing_field: None,
            error: None,
        });
    }

    #[test]
    fn editor_rejects_empty_name() {
        let mut st = state_with(vec![]);
        open_editor(&mut st, profile("  ", ""), None);
        assert!(!st.save_edited_profile());
        assert!(st.editor.as_ref().is_some_and(|e| e.error.is_some()));
        assert!(st.profiles.is_empty());
    }

    #[test]
    fn editor_rejects_duplicate_game_name() {
        let mut st = state_with(vec![profile("A", "sf2"), profile("B", "")]);
        open_editor(&mut st, profile("B", "sf2"), Some(1));
        assert!(!st.save_edited_profile());
        assert_eq!(st.profiles[1].game_name_str(), "");

        // Keeping its own game name is fine.
        open_editor(&mut st, profile("A2", "sf2"), Some(0));
        assert!(st.save_edited_profile());
        assert_eq!(names(&st), ["A2", "B"]);
        assert!(st.editor.is_none());
    }

    #[test]
    fn editor_adds_new_profile() {
        let mut st = state_with(vec![profile("A", "")]);
        open_editor(&mut st, profile("New", "hotd"), None);
        assert!(st.save_edited_profile());
        assert_eq!(names(&st), ["A", "New"]);
        assert!(st.is_dirty);
    }
}
