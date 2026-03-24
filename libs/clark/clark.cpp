// clark.cpp
// Clark library (Native GUI with WebView) for Atomic
//
// Creates native windows with HTML/CSS/JS content using WebView2 (Windows) or WebKitGTK (Linux).
// All functions receive/return int64_t. Strings = char* cast to int64_t.
//
// Build (Windows):
//   g++ -shared -o libs/clark/clark.dla libs/clark/clark.cpp -O3 -std=c++14 -I libs/clark/src/webview/core/include -I libs/clark/src/webview2/build/native/include -static -mwindows -ladvapi32 -lole32 -lshell32 -lshlwapi -luser32 -lversion -ldwmapi
//
// Build (Linux):
//   g++ -shared -fPIC -o libs/clark/clark.dla libs/clark/clark.cpp -O3 -std=c++14 -I libs/clark/src/webview/core/include $(pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.0) -lpthread

#ifdef _WIN32
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0A00
    #endif
#endif

#define WEBVIEW_STATIC
#include "webview/webview.h"

#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <cstdio>

#ifdef _WIN32
    #include <windows.h>
    #include <dwmapi.h>
#else
    #include <gtk/gtk.h>
    #include <gdk/gdkx.h>
#endif

// =============================================================================
// EXPORT
// =============================================================================
#ifdef _WIN32
    #define AT_EXPORT extern "C" __declspec(dllexport)
#else
    #define AT_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// =============================================================================
// ROTATING BUFFER FOR STRING RETURNS
// =============================================================================
static char str_bufs[8][4096];
static int str_buf_idx = 0;
static std::mutex str_buf_mutex;

static int64_t return_str(const std::string& s) {
    std::lock_guard<std::mutex> lock(str_buf_mutex);
    char* buf = str_bufs[str_buf_idx++ & 7];
    strncpy(buf, s.c_str(), 4095);
    buf[4095] = '\0';
    return (int64_t)buf;
}

// =============================================================================
// DATA STRUCTURES
// =============================================================================
struct Window {
    webview::webview* wv;
    std::thread wv_thread;
    std::atomic<bool> active;
    std::atomic<bool> ready;

    // Event queue (binds called from JS)
    struct Event {
        std::string name;
        std::string value;
    };
    std::queue<Event> event_queue;
    std::mutex events_mutex;
    std::string last_value;

    // Command queue (Atomic -> WebView)
    struct Command {
        enum Type { CMD_HTML, CMD_JS, CMD_TITLE, CMD_SIZE, CMD_FILE, CMD_BIND, CMD_CLOSE, CMD_BORDERLESS };
        Type type;
        std::string arg1;
        int width;
        int height;
    };
    std::queue<Command> command_queue;
    std::mutex commands_mutex;

    std::string initial_title;
    int initial_width;
    int initial_height;
    bool borderless;

    Window() : wv(nullptr), active(false), ready(false), initial_width(800), initial_height(600), borderless(false) {}
};

// =============================================================================
// GLOBAL STATE
// =============================================================================
static std::unordered_map<int, Window*> windows;
static std::mutex windows_mutex;
static std::atomic<int> next_id{1};

// =============================================================================
// CUSTOM TITLEBAR JS
// =============================================================================
static const char* CLARK_TITLEBAR_JS = R"JS(
(function() {
    function __clark_inject_bar() {
        if (document.querySelector('.__clark_titlebar')) return;
        var style = document.createElement('style');
        style.textContent = `
            .__clark_titlebar {
                position: fixed;
                top: 0; left: 0; right: 0;
                height: 32px;
                background: rgba(30,30,30,0.95);
                display: flex;
                align-items: center;
                z-index: 999999;
                user-select: none;
                -webkit-user-select: none;
                font-family: 'Segoe UI', Arial, sans-serif;
                font-size: 12px;
                color: rgba(255,255,255,0.85);
            }
            .__clark_titlebar_drag {
                flex: 1;
                height: 100%;
                display: flex;
                align-items: center;
                padding-left: 12px;
                cursor: default;
            }
            .__clark_titlebar_btns {
                display: flex;
                height: 100%;
            }
            .__clark_titlebar_btn {
                width: 46px;
                height: 100%;
                border: none;
                background: transparent;
                color: rgba(255,255,255,0.85);
                font-size: 10px;
                cursor: pointer;
                display: flex;
                align-items: center;
                justify-content: center;
                transition: background 0.15s;
            }
            .__clark_titlebar_btn:hover {
                background: rgba(255,255,255,0.1);
            }
            .__clark_titlebar_btn.__clark_close:hover {
                background: #e81123;
                color: white;
            }
            .__clark_titlebar_btn svg {
                width: 10px;
                height: 10px;
                fill: none;
                stroke: currentColor;
                stroke-width: 1.2;
            }
            body {
                padding-top: 32px !important;
            }
        `;
        document.head.appendChild(style);
        var bar = document.createElement('div');
        bar.className = '__clark_titlebar';
        bar.innerHTML = `
            <div class="__clark_titlebar_drag" onmousedown="__clark_move()">
                <span>${document.title || ''}</span>
            </div>
            <div class="__clark_titlebar_btns">
                <button class="__clark_titlebar_btn" onclick="__clark_minimize()" title="Minimize">
                    <svg viewBox="0 0 10 10"><line x1="0" y1="5" x2="10" y2="5"/></svg>
                </button>
                <button class="__clark_titlebar_btn" onclick="__clark_maximize()" title="Maximize">
                    <svg viewBox="0 0 10 10"><rect x="0.5" y="0.5" width="9" height="9"/></svg>
                </button>
                <button class="__clark_titlebar_btn __clark_close" onclick="__clark_close()" title="Close">
                    <svg viewBox="0 0 10 10"><line x1="0" y1="0" x2="10" y2="10"/><line x1="10" y1="0" x2="0" y2="10"/></svg>
                </button>
            </div>
        `;
        document.body.prepend(bar);
    }
    if (document.body) {
        __clark_inject_bar();
    } else {
        document.addEventListener('DOMContentLoaded', __clark_inject_bar);
        var __clark_poll = setInterval(function() {
            if (document.body) {
                clearInterval(__clark_poll);
                __clark_inject_bar();
            }
        }, 50);
    }
})();
)JS";

static void clark_register_titlebar_init(Window* win) {
    if (win->wv) {
        win->wv->init(CLARK_TITLEBAR_JS);
    }
}

// =============================================================================
// WEBVIEW THREAD
// =============================================================================
static void webview_thread_func(Window* win) {
    try {
        win->wv = new webview::webview(false, nullptr);
        win->wv->set_title(win->initial_title);
        win->wv->set_size(win->initial_width, win->initial_height, WEBVIEW_HINT_NONE);
        win->active.store(true);
        win->ready.store(true);

        win->wv->run();

        win->active.store(false);
        delete win->wv;
        win->wv = nullptr;
    } catch (...) {
        win->active.store(false);
        win->ready.store(true);
    }
}

static void process_commands(Window* win) {
    std::queue<Window::Command> cmds;
    {
        std::lock_guard<std::mutex> lock(win->commands_mutex);
        std::swap(cmds, win->command_queue);
    }

    while (!cmds.empty()) {
        auto& cmd = cmds.front();
        switch (cmd.type) {
            case Window::Command::CMD_HTML:
                if (win->wv) {
                    win->wv->set_html(cmd.arg1);
                    if (win->borderless) {
                        clark_register_titlebar_init(win);
                    }
                }
                break;
            case Window::Command::CMD_JS:
                if (win->wv) win->wv->eval(cmd.arg1);
                break;
            case Window::Command::CMD_TITLE:
                if (win->wv) win->wv->set_title(cmd.arg1);
                break;
            case Window::Command::CMD_SIZE:
                if (win->wv) win->wv->set_size(cmd.width, cmd.height, WEBVIEW_HINT_NONE);
                break;
            case Window::Command::CMD_FILE: {
                FILE* f = fopen(cmd.arg1.c_str(), "rb");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long size = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    std::string content(size, '\0');
                    fread(&content[0], 1, size, f);
                    fclose(f);
                    if (win->wv) {
                        win->wv->set_html(content);
                        if (win->borderless) {
                            clark_register_titlebar_init(win);
                        }
                    }
                }
                break;
            }
            case Window::Command::CMD_BIND: {
                std::string bind_name = cmd.arg1;
                if (win->wv) {
                    win->wv->bind(bind_name, [win, bind_name](const std::string& args) -> std::string {
                        std::string value = "";
                        if (args.size() > 2) {
                            std::string inner = args.substr(1, args.size() - 2);
                            if (inner.size() >= 2 && inner[0] == '"') {
                                size_t end = inner.find('"', 1);
                                if (end != std::string::npos) {
                                    value = inner.substr(1, end - 1);
                                }
                            } else {
                                size_t end = inner.find(',');
                                value = (end != std::string::npos) ? inner.substr(0, end) : inner;
                            }
                        }
                        std::lock_guard<std::mutex> lock(win->events_mutex);
                        Window::Event ev;
                        ev.name = bind_name;
                        ev.value = value;
                        win->event_queue.push(ev);
                        return "";
                    });
                }
                break;
            }
            case Window::Command::CMD_CLOSE:
                if (win->wv) win->wv->terminate();
                break;
            case Window::Command::CMD_BORDERLESS: {
                if (win->wv) {
#ifdef _WIN32
                    HWND hwnd = (HWND)win->wv->window().value();
                    if (hwnd) {
                        LONG style = GetWindowLong(hwnd, GWL_STYLE);
                        style &= ~(WS_CAPTION | WS_THICKFRAME);
                        style |= WS_POPUP;
                        SetWindowLong(hwnd, GWL_STYLE, style);

                        LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
                        exStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
                        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);

                        MARGINS margins = {1, 1, 1, 1};
                        DwmExtendFrameIntoClientArea(hwnd, &margins);

                        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);

                        win->borderless = true;

                        win->wv->bind("__clark_minimize", [hwnd](const std::string&) -> std::string {
                            ShowWindow(hwnd, SW_MINIMIZE);
                            return "";
                        });
                        win->wv->bind("__clark_maximize", [hwnd](const std::string&) -> std::string {
                            if (IsZoomed(hwnd)) {
                                ShowWindow(hwnd, SW_RESTORE);
                            } else {
                                ShowWindow(hwnd, SW_MAXIMIZE);
                            }
                            return "";
                        });
                        win->wv->bind("__clark_close", [win](const std::string&) -> std::string {
                            if (win->wv) win->wv->terminate();
                            return "";
                        });
                        win->wv->bind("__clark_move", [hwnd](const std::string&) -> std::string {
                            ReleaseCapture();
                            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                            return "";
                        });

                        clark_register_titlebar_init(win);
                        win->wv->eval(CLARK_TITLEBAR_JS);
                    }
#else
                    GtkWidget* window_widget = (GtkWidget*)win->wv->window().value();
                    GtkWindow* gtk_win = GTK_WINDOW(window_widget);
                    if (gtk_win) {
                        gtk_window_set_decorated(gtk_win, FALSE);

                        win->borderless = true;

                        win->wv->bind("__clark_minimize", [gtk_win](const std::string&) -> std::string {
                            gtk_window_iconify(gtk_win);
                            return "";
                        });
                        win->wv->bind("__clark_maximize", [gtk_win](const std::string&) -> std::string {
                            if (gtk_window_is_maximized(gtk_win)) {
                                gtk_window_unmaximize(gtk_win);
                            } else {
                                gtk_window_maximize(gtk_win);
                            }
                            return "";
                        });
                        win->wv->bind("__clark_close", [win](const std::string&) -> std::string {
                            if (win->wv) win->wv->terminate();
                            return "";
                        });
                        win->wv->bind("__clark_move", [gtk_win](const std::string&) -> std::string {
                            GdkWindow* gdk_win = gtk_widget_get_window(GTK_WIDGET(gtk_win));
                            if (gdk_win) {
                                GdkDevice* device = nullptr;
                                GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default());
                                if (seat) {
                                    device = gdk_seat_get_pointer(seat);
                                }
                                if (device) {
                                    gint x, y;
                                    gdk_device_get_position(device, NULL, &x, &y);
                                    gtk_window_begin_move_drag(gtk_win, 1, x, y, GDK_CURRENT_TIME);
                                }
                            }
                            return "";
                        });

                        clark_register_titlebar_init(win);
                        win->wv->eval(CLARK_TITLEBAR_JS);
                    }
#endif
                }
                break;
            }
        }
        cmds.pop();
    }
}

static void send_command(Window* win, Window::Command cmd) {
    {
        std::lock_guard<std::mutex> lock(win->commands_mutex);
        win->command_queue.push(cmd);
    }
    if (win->wv) {
        win->wv->dispatch([win](void) {
            process_commands(win);
        });
    }
}

// =============================================================================
// EXPORTED FUNCTIONS
// =============================================================================

// clark_create(title, width, height) -> int (window id)
AT_EXPORT int64_t clark_create(int64_t title, int64_t width, int64_t height) {
    Window* win = new Window();
    win->initial_title = std::string((const char*)title);
    win->initial_width = (int)width;
    win->initial_height = (int)height;

    int id = next_id.fetch_add(1);

    {
        std::lock_guard<std::mutex> lock(windows_mutex);
        windows[id] = win;
    }

    win->wv_thread = std::thread(webview_thread_func, win);

    while (!win->ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    printf("[Clark] Window %d created (%dx%d)\n", id, (int)width, (int)height);
    return (int64_t)id;
}

// clark_html(window, html) -> int
AT_EXPORT int64_t clark_html(int64_t window_id, int64_t html) {
    std::lock_guard<std::mutex> lock(windows_mutex);
    auto it = windows.find((int)window_id);
    if (it == windows.end() || !it->second->active.load()) return 0;

    Window::Command cmd;
    cmd.type = Window::Command::CMD_HTML;
    cmd.arg1 = std::string((const char*)html);
    send_command(it->second, cmd);
    return 1;
}

// clark_file(window, path) -> int
AT_EXPORT int64_t clark_file(int64_t window_id, int64_t path) {
    std::lock_guard<std::mutex> lock(windows_mutex);
    auto it = windows.find((int)window_id);
    if (it == windows.end() || !it->second->active.load()) return 0;

    Window::Command cmd;
    cmd.type = Window::Command::CMD_FILE;
    cmd.arg1 = std::string((const char*)path);
    send_command(it->second, cmd);
    return 1;
}

// clark_js(window, code) -> int
AT_EXPORT int64_t clark_js(int64_t window_id, int64_t code) {
    std::lock_guard<std::mutex> lock(windows_mutex);
    auto it = windows.find((int)window_id);
    if (it == windows.end() || !it->second->active.load()) return 0;

    Window::Command cmd;
    cmd.type = Window::Command::CMD_JS;
    cmd.arg1 = std::string((const char*)code);
    send_command(it->second, cmd);
    return 1;
}

// clark_bind(window, name) -> int
AT_EXPORT int64_t clark_bind(int64_t window_id, int64_t name) {
    std::lock_guard<std::mutex> lock(windows_mutex);
    auto it = windows.find((int)window_id);
    if (it == windows.end() || !it->second->active.load()) return 0;

    Window::Command cmd;
    cmd.type = Window::Command::CMD_BIND;
    cmd.arg1 = std::string((const char*)name);
    send_command(it->second, cmd);
    return 1;
}

// clark_active(window) -> int (1 = open, 0 = closed)
AT_EXPORT int64_t clark_active(int64_t window_id) {
    std::lock_guard<std::mutex> lock(windows_mutex);
    auto it = windows.find((int)window_id);
    if (it == windows.end()) return 0;
    return it->second->active.load() ? 1 : 0;
}

// clark_event(window) -> string (event name or "")
AT_EXPORT int64_t clark_event(int64_t window_id) {
    Window* win = nullptr;
    {
        std::lock_guard<std::mutex> lock(windows_mutex);
        auto it = windows.find((int)window_id);
        if (it == windows.end()) return return_str("");
        win = it->second;
    }

    std::lock_guard<std::mutex> lock(win->events_mutex);
    if (win->event_queue.empty()) return return_str("");
    Window::Event ev = win->event_queue.front();
    win->event_queue.pop();
    win->last_value = ev.value;
    return return_str(ev.name);
}

// clark_js_value(window) -> string (last event value)
AT_EXPORT int64_t clark_js_value(int64_t window_id) {
    Window* win = nullptr;
    {
        std::lock_guard<std::mutex> lock(windows_mutex);
        auto it = windows.find((int)window_id);
        if (it == windows.end()) return return_str("");
        win = it->second;
    }

    std::lock_guard<std::mutex> lock(win->events_mutex);
    return return_str(win->last_value);
}

// clark_title(window, text) -> int
AT_EXPORT int64_t clark_title(int64_t window_id, int64_t text) {
    std::lock_guard<std::mutex> lock(windows_mutex);
    auto it = windows.find((int)window_id);
    if (it == windows.end() || !it->second->active.load()) return 0;

    Window::Command cmd;
    cmd.type = Window::Command::CMD_TITLE;
    cmd.arg1 = std::string((const char*)text);
    send_command(it->second, cmd);
    return 1;
}

// clark_size(window, width, height) -> int
AT_EXPORT int64_t clark_size(int64_t window_id, int64_t width, int64_t height) {
    std::lock_guard<std::mutex> lock(windows_mutex);
    auto it = windows.find((int)window_id);
    if (it == windows.end() || !it->second->active.load()) return 0;

    Window::Command cmd;
    cmd.type = Window::Command::CMD_SIZE;
    cmd.width = (int)width;
    cmd.height = (int)height;
    send_command(it->second, cmd);
    return 1;
}

// clark_borderless(window) -> int
AT_EXPORT int64_t clark_borderless(int64_t window_id) {
    std::lock_guard<std::mutex> lock(windows_mutex);
    auto it = windows.find((int)window_id);
    if (it == windows.end() || !it->second->active.load()) return 0;

    Window::Command cmd;
    cmd.type = Window::Command::CMD_BORDERLESS;
    send_command(it->second, cmd);
    return 1;
}

// clark_close(window) -> int
AT_EXPORT int64_t clark_close(int64_t window_id) {
    Window* win = nullptr;
    {
        std::lock_guard<std::mutex> lock(windows_mutex);
        auto it = windows.find((int)window_id);
        if (it == windows.end()) return 0;
        win = it->second;
    }

    Window::Command cmd;
    cmd.type = Window::Command::CMD_CLOSE;
    send_command(win, cmd);

    if (win->wv_thread.joinable()) {
        win->wv_thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(windows_mutex);
        windows.erase((int)window_id);
    }

    delete win;
    printf("[Clark] Window %d closed\n", (int)window_id);
    return 1;
}