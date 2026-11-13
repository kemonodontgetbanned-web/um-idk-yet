#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>

#include <fcntl.h>
#include <linux/input.h>
#include <pwd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"


#include "Offsets.hpp"

static GtkWidget* g_area = nullptr;
static GtkWidget* g_win = nullptr;
static gint64 g_last_us = 0;
static std::mutex g_mx;
static int g_kfds[32];
static int g_nkfds = 0;
static bool g_keysAvail = false;
static bool g_pgWas = false;


static char g_title[128] = "RBX Overlay [...]";
static float g_wsVal = 100.0f;
static std::string g_wsStatus = "idle";
static bool g_wsBusy = false;
static int g_fpsDetected = -1;
static int g_fpsEdit = 144;
static std::string g_fpsStatus = "";
static bool g_flyOn = false;
static int g_flyMode = 0;  
static float g_flySpeed = 100.0f;
static pid_t g_flyPid = -1;
static int g_flySnapMode = 0;
static float g_flySnapSpeed = 0;
static std::string g_flyStatus = "off";
static unsigned long g_logTick = 0;

static std::string ToolsDir() {
    char b[4096] = {};
    ssize_t n = readlink("/proc/self/exe", b, sizeof(b) - 1);
    std::string s(b, n > 0 ? n : 0);
    size_t sl = s.find_last_of('/');
    return (sl == std::string::npos ? std::string(".") : s.substr(0, sl)) +
           "/../tools";
}

static std::string HomeDir() {
    
    const char* su = getenv("SUDO_USER");
    if (su) {
        struct passwd* pw = getpwnam(su);
        if (pw && pw->pw_dir) return pw->pw_dir;
    }
    const char* h = getenv("HOME");
    return h ? h : "";
}

static std::string XmlPath() {
    std::string home = HomeDir();
    if (home.empty()) return "";
    return home +
           "/.var/app/org.vinegarhq.Sober/data/sober/appData/"
           "GlobalBasicSettings_13.xml";
}


static void InitPageUp() {
    for (int i = 0; i < 32; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        unsigned char caps[(KEY_MAX + 8) / 8] = {};
        bool keep = false;
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(caps)), caps) >= 0)
            keep = (caps[KEY_PAGEUP / 8] >> (KEY_PAGEUP % 8)) & 1;
        if (keep && g_nkfds < 32)
            g_kfds[g_nkfds++] = fd;
        else
            close(fd);
    }
    g_keysAvail = g_nkfds > 0;
}

static int RunCapture(const std::string& cmd, std::string& out) {
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) {
        out = "spawn failed";
        return 127;
    }
    char b[512];
    out.clear();
    while (fgets(b, sizeof(b), p)) {
        out += b;
        if (out.size() > 2000) out.erase(0, out.size() - 2000);
    }
    int rc = pclose(p);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : 128;
}

static std::string LastLines(const std::string& s, int n) {
    size_t p = s.size();
    int count = 0;
    while (p > 0 && count <= n) {
        if (s[--p] == '\n') ++count;
    }
    size_t start = (p == 0) ? 0 : p + 1;
    std::string r = s.substr(start);
    while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) r.pop_back();
    return r.empty() ? "(no output)" : r;
}

static int ReadFpsCap() {
    std::string path = XmlPath();
    if (path.empty()) return -1;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return -1;
    char* ln = nullptr;
    size_t cp = 0;
    ssize_t n;
    int cap = -1;
    while ((n = getline(&ln, &cp, f)) != -1) {
        const char* p = strstr(ln, "FramerateCap");
        if (!p) continue;
        const char* q = strchr(p, '>');
        if (q) cap = atoi(q + 1);
    }
    fclose(f);
    free(ln);
    return cap;
}

static std::string WriteFpsCap(int cap) {
    std::string path = XmlPath();
    if (path.empty()) return "no HOME";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "cannot open xml";
    std::string data;
    char b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) data.append(b, n);
    fclose(f);
    const char* key = "<int name=\"FramerateCap\">";
    size_t p = data.find(key);
    if (p == std::string::npos) return "FramerateCap tag not found";
    size_t e = data.find('<', p + strlen(key));
    if (e == std::string::npos) return "malformed tag";
    data.replace(p + strlen(key), e - p - strlen(key), std::to_string(cap));
    f = fopen(path.c_str(), "wb");
    if (!f) return "cannot write xml";
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return "applied (restart game; client may revert)";
}

static std::string LogTail() {
    FILE* f = fopen("/tmp/rbxfly.log", "r");
    if (!f) return "(no log)";
    std::string data;
    char b[512];
    size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) {
        data += std::string(b, n);
        if (data.size() > 512) data.erase(0, data.size() - 512);
    }
    fclose(f);
    return LastLines(data, 1);
}

static void ProbeGame() {
    std::string out;
    int rc = RunCapture(ToolsDir() + "/write_ws --probe 2>&1", out);
    std::lock_guard<std::mutex> l(g_mx);
    if (rc == 0)
        snprintf(g_title, sizeof(g_title), "RBX Overlay [OK]");
    else
        snprintf(g_title, sizeof(g_title), "RBX Overlay [NO GAME]");
}

static void FlyStop() {
    if (g_flyPid > 0) kill(g_flyPid, SIGINT);
}

static void FlySpawn() {
    FlyStop();
    const char* bin = g_flyMode == 0 ? "cframe_fly" : "velocity_fly";
    char speed[32];
    snprintf(speed, sizeof(speed), "%.0f", (double)g_flySpeed);
    std::string tool = ToolsDir() + "/" + bin;
    pid_t pid = fork();
    if (pid == 0) {
        int fd =
            open("/tmp/rbxfly.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
            close(fd);
        }
        execl(tool.c_str(), bin, speed, (char*)nullptr);
        _exit(127);
    }
    g_flyPid = pid;
    g_flySnapMode = g_flyMode;
    g_flySnapSpeed = g_flySpeed;
    g_flyStatus = std::string("running (") + bin + ")";
}

static gboolean on_render(GtkGLArea* area, GdkGLContext* ,
                          gpointer ) {
    int w = gtk_widget_get_width(GTK_WIDGET(area));
    int h = gtk_widget_get_height(GTK_WIDGET(area));
    int scale = gtk_widget_get_scale_factor(GTK_WIDGET(area));
    if (w <= 0 || h <= 0) return TRUE;

    gint64 now = g_get_monotonic_time();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.DisplayFramebufferScale = ImVec2((float)scale, (float)scale);
    io.DeltaTime = g_last_us > 0 ? (float)(now - g_last_us) / 1000000.0f
                                 : 1.0f / 60.0f;
    g_last_us = now;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    char title[128];
    {
        std::lock_guard<std::mutex> l(g_mx);
        memcpy(title, g_title, sizeof(title));
    }
    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 560), ImGuiCond_FirstUseEver);
    ImGui::Begin(title);

    
    ImGui::SeparatorText("WALKSPEED");
    ImGui::InputFloat("value", &g_wsVal, 1.0f, 10.0f, "%.1f");
    ImGui::SameLine();
    bool busy;
    {
        std::lock_guard<std::mutex> l(g_mx);
        busy = g_wsBusy;
    }
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("WRITE")) {
        std::lock_guard<std::mutex> l(g_mx);
        g_wsBusy = true;
        g_wsStatus = "writing...";
        float v = g_wsVal;
        std::thread([v] {
            std::string cmd =
                ToolsDir() + "/write_ws " + std::to_string(v) + " 2>&1";
            std::string out;
            int rc = RunCapture(cmd, out);
            std::lock_guard<std::mutex> l(g_mx);
            g_wsStatus =
                (rc == 0 ? "OK: " : "FAIL: ") + LastLines(out, 3);
            g_wsBusy = false;
        }).detach();
    }
    ImGui::EndDisabled();
    {
        std::lock_guard<std::mutex> l(g_mx);
        ImGui::TextWrapped("%s", g_wsStatus.c_str());
    }

    
    ImGui::SeparatorText("MAX FPS");
    {
        std::lock_guard<std::mutex> l(g_mx);
        ImGui::Text("Detected cap: %d", g_fpsDetected);
    }
    ImGui::InputInt("cap", &g_fpsEdit);
    ImGui::SameLine();
    if (ImGui::Button("APPLY")) {
        int v = g_fpsEdit;
        std::string r = WriteFpsCap(v);
        std::lock_guard<std::mutex> l(g_mx);
        g_fpsStatus = r;
        g_fpsDetected = ReadFpsCap();
    }
    {
        std::lock_guard<std::mutex> l(g_mx);
        if (!g_fpsStatus.empty()) ImGui::TextWrapped("%s", g_fpsStatus.c_str());
    }

    
    ImGui::SeparatorText("FLY");
    bool on;
    {
        std::lock_guard<std::mutex> l(g_mx);
        on = g_flyOn;
    }
    if (ImGui::Button(on ? "FLY: ON" : "FLY: OFF")) {
        std::lock_guard<std::mutex> l(g_mx);
        g_flyOn = !g_flyOn;
        if (g_flyOn)
            FlySpawn();
        else
            FlyStop();
    }
    static const char* kModes[] = {"CFrame", "Velocity"};
    if (ImGui::Combo("Mode", &g_flyMode, kModes, 2)) {
        std::lock_guard<std::mutex> l(g_mx);
        if (g_flyOn) FlySpawn();  
    }
    if (ImGui::InputFloat("Speed", &g_flySpeed, 10.0f, 100.0f, "%.0f")) {
        std::lock_guard<std::mutex> l(g_mx);
        if (g_flyOn) FlySpawn();  
    }
    {
        std::lock_guard<std::mutex> l(g_mx);
        ImGui::TextWrapped("%s", g_flyStatus.c_str());
    }

    if (ImGui::Button("RESCAN")) {
        std::thread([] {
            ProbeGame();
        }).detach();
    }
    ImGui::TextDisabled("PageUp: show/hide panel");
    if (!g_keysAvail)
        ImGui::TextDisabled("hotkey off: no /dev/input access");
    ImGui::TextDisabled("build WS 0x%x Prim 0x%x VE 0x%x",
                        (unsigned)Offsets::Humanoid::Walkspeed,
                        (unsigned)Offsets::BasePart::Primitive,
                        (unsigned)Offsets::VisualEngine::Pointer);
    ImGui::End();

    ImGui::Render();
    glViewport(0, 0, w * scale, h * scale);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return TRUE;
}

static gboolean tick(gpointer) {
    
    if (g_keysAvail && g_win) {
        unsigned char acc[(KEY_MAX + 8) / 8] = {};
        for (int i = 0; i < g_nkfds; ++i) {
            unsigned char bits[(KEY_MAX + 8) / 8] = {};
            if (ioctl(g_kfds[i], EVIOCGKEY(sizeof(bits)), bits) < 0) continue;
            for (size_t b = 0; b < sizeof(acc); ++b) acc[b] |= bits[b];
        }
        bool down = (acc[KEY_PAGEUP / 8] >> (KEY_PAGEUP % 8)) & 1;
        if (down && !g_pgWas)
            gtk_widget_set_visible(g_win, !gtk_widget_get_visible(g_win));
        g_pgWas = down;
    }
    
    if (g_flyPid > 0) {
        int st = 0;
        pid_t r = waitpid(g_flyPid, &st, WNOHANG);
        std::lock_guard<std::mutex> l(g_mx);
        if (r == g_flyPid) {
            g_flyPid = -1;
            if (g_flyOn) {
                g_flyOn = false;
                g_flyStatus = std::string("fly died: ") + LogTail();
            }
        } else if (++g_logTick % 30 == 0) {
            g_flyStatus = std::string("running — ") + LogTail();
        }
    }
    if (g_area && g_win && gtk_widget_get_visible(g_win))
        gtk_gl_area_queue_render(GTK_GL_AREA(g_area));
    return G_SOURCE_CONTINUE;
}

static void on_motion(GtkEventControllerMotion* , double x, double y,
                      gpointer) {
    ImGui::GetIO().MousePos = ImVec2((float)x, (float)y);
}

static void on_press(GtkGestureClick* , int , double ,
                     double , gpointer) {
    ImGui::GetIO().MouseDown[0] = true;
}

static void on_release(GtkGestureClick* , int , double ,
                       double , gpointer) {
    ImGui::GetIO().MouseDown[0] = false;
}

static void on_activate(GtkApplication* app, gpointer) {
    GtkWidget* win = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), "rbx-overlay");
    gtk_window_set_default_size(GTK_WINDOW(win), 0, 0);

    gtk_layer_init_for_window(GTK_WINDOW(win));
    gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_namespace(GTK_WINDOW(win), "rbx-overlay");
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(win), 0);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(win),
                                GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

    GdkDisplay* dpy = gdk_display_get_default();
    GListModel* mons = gdk_display_get_monitors(dpy);
    guint n = g_list_model_get_n_items(mons);
    GdkMonitor* pick = nullptr;
    for (guint i = 0; i < n; ++i) {
        GdkMonitor* m = GDK_MONITOR(g_list_model_get_item(mons, i));
        GdkRectangle g;
        gdk_monitor_get_geometry(m, &g);
        printf("monitor %u: x=%d y=%d %dx%d\n", i, g.x, g.y, g.width,
               g.height);
        if (!pick || g.x == 1920) pick = m;
        if (g.x == 1920) {
            printf("overlay target: monitor %u\n", i);
            break;
        }
    }
    if (pick) gtk_layer_set_monitor(GTK_WINDOW(win), pick);

    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css,
                                      "window { background: transparent; }");
    gtk_style_context_add_provider_for_display(
        dpy, GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    g_area = gtk_gl_area_new();
    gtk_widget_set_hexpand(g_area, TRUE);
    gtk_widget_set_vexpand(g_area, TRUE);
    g_signal_connect(g_area, "render", G_CALLBACK(on_render), nullptr);
    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), nullptr);
    gtk_widget_add_controller(win, motion);
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    g_signal_connect(click, "pressed", G_CALLBACK(on_press), nullptr);
    g_signal_connect(click, "released", G_CALLBACK(on_release), nullptr);
    gtk_widget_add_controller(win, GTK_EVENT_CONTROLLER(click));
    gtk_window_set_child(GTK_WINDOW(win), g_area);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 8.0f;
    st.FrameRounding = 6.0f;
    st.GrabRounding = 6.0f;
    st.FrameBorderSize = 1.0f;
    gtk_widget_realize(g_area);
    gtk_gl_area_make_current(GTK_GL_AREA(g_area));
    
    ImGui_ImplOpenGL3_Init("#version 100");

    
    g_fpsDetected = ReadFpsCap();
    g_fpsEdit = g_fpsDetected > 0 ? g_fpsDetected : 144;
    g_win = win;
    InitPageUp();
    std::thread([] {
        ProbeGame();
    }).detach();

    gtk_window_present(GTK_WINDOW(win));
    g_timeout_add(16, tick, nullptr);
}

int main(int argc, char** argv) {
    
    
    {
        const char* sudoUid = getenv("SUDO_UID");
        long uid = sudoUid ? atol(sudoUid) : (long)getuid();
        if (!getenv("XDG_RUNTIME_DIR")) {
            char rt[64];
            snprintf(rt, sizeof(rt), "/run/user/%ld", uid);
            setenv("XDG_RUNTIME_DIR", rt, 0);
        }
        if (!getenv("WAYLAND_DISPLAY")) {
            char ws[128], sock[160];
            snprintf(ws, sizeof(ws), "wayland-1");
            snprintf(sock, sizeof(sock), "/run/user/%ld/wayland-1", uid);
            if (access(sock, F_OK) == 0) setenv("WAYLAND_DISPLAY", ws, 0);
        }
    }
    GtkApplication* app = gtk_application_new("lol.imtheo.rbxoverlay",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    int st = g_application_run(G_APPLICATION(app), argc, argv);
    if (g_flyPid > 0) {
        kill(g_flyPid, SIGINT);
        waitpid(g_flyPid, nullptr, 0);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    g_object_unref(app);
    return st;
}
