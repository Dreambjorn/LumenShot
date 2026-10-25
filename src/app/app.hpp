#pragma once
#include "app/workflow.hpp"
#include "app/diagnostics.hpp"
#include "platform/startup.hpp"
#include "ui/theme.hpp"
#include "ui/notices.hpp"
#include "ui/selector.hpp"

namespace hdrshot {

inline constexpr wchar_t window_class[] = L"LumenShotAppWindow";
inline constexpr UINT done_message = WM_APP + 1, tray_message = WM_APP + 2;
inline constexpr int hotkey_id = 1, cancel_hotkey_id = 2, tray_id = 1, countdown_timer = 1, settings_timer = 2, hotkey_retry_timer = 3;
enum ControlId { Folder=100, Browse, MonitorChoice, BackendChoice, ContentChoice, PaperWhite,
                 Exposure, KeepOriginal, ModifierCtrl, ModifierShift, ModifierAlt, KeyChoice,
                 CaptureButton, SaveButton=114, FolderButton, ExitButton, Status, HdrFormatChoice, StartWithWindows, DarkMode, SettingsTabs, Sharpness, SharpnessValue, ImageSize, LicensesButton, PeakNits=127, ShowMagnifier, HdrAnnotationChoice, HdrAnnotationLabel, HdrPrivacyLabel, WindowSnapping };
enum SettingsPage { CapturePage, ImagePage, OutputPage, GeneralPage, SettingsPageCount };
inline constexpr int settings_width=714,settings_height=650;

struct App {
    HWND window=nullptr,status=nullptr,menu_window=nullptr;
    HFONT body_font=nullptr,title_font=nullptr,small_font=nullptr;
    NativeTheme theme;
    LicenseViewer licenses;
    int active_page=0,creating_page=-1;
    std::vector<std::pair<HWND,int>> page_controls;
    Settings settings=load_settings();
    Settings capture_settings=settings;
    std::vector<Monitor> displays;
    NativeWork worker,warmup;
    StartupRegistration startup;
    bool busy=false,exit_after_work=false,tray_added=false;
    bool preparing=false,cancel_requested=false,escape_registered=false;
    RegionSelector* active_selector=nullptr;
    int countdown=0;
    bool controls_ready=false;
    UINT pending_edits=0,invalid_edits=0;
    UINT registered_modifiers=0,registered_key=0;
    WorkResult result;
    std::wstring subtitle;
    bool diagnostics_enabled=false;
    std::shared_ptr<DiagnosticRecord> capture_diagnostics;
    ~App();
    HWND control(const wchar_t* type,const wchar_t* label,DWORD style,int id,int x,int y,int w,int h);
    void label(const wchar_t* value,int x,int y,int width);
    void add_combo(int id,const std::vector<std::wstring>& entries,int selection,int x,int y,int width);
    void notify(const std::wstring& title,const std::wstring& message,DWORD flags=NIIF_INFO);
    void paint(HDC dc);
    void set_status(const std::wstring& message);
    void register_hotkey();
    void retry_hotkey();
    void create_controls();
    void show_page(int page);
    void update_hdr_choice();
    void update_sharpness_label();
    void read_controls(int changed=0);
    bool apply_setting(int id);
    void queue_edit(int id);
    bool flush_edits();
    void listen_for_escape();
    void release_escape();
    void cancel_capture();
    void begin_work();
    void finish_work();
    void browse();
    void tray_menu();
};

std::wstring text(HWND control);
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wp,LPARAM lp);

} // namespace hdrshot
