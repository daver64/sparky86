#include "TextEditor.h"
#include "keymap.h"
#include "system_bus.h"
#include "x86cpu.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Provided by main.cpp: reload pmode_core.bin into a fresh guest and start it
bool restart_guest(bool paused);

// Registers can only be read while the CPU is parked, so the last parked snapshot is kept.
static void draw_registers_window(X86Cpu &cpu)
{
    static X86Cpu::Registers snap{};
    const bool live = cpu.is_paused() && cpu.is_parked();
    if (live)
        snap = cpu.registers();

    ImGui::SetNextWindowSize(ImVec2(280, 330), ImGuiCond_FirstUseEver);
    ImGui::Begin("Registers");
    if (!live)
        ImGui::TextDisabled("Running - showing last paused values");
    ImGui::Text("Instructions: %llu", (unsigned long long)cpu.instruction_count());
    ImGui::Separator();
    ImGui::Text("EAX %08X   EBX %08X", snap.eax, snap.ebx);
    ImGui::Text("ECX %08X   EDX %08X", snap.ecx, snap.edx);
    ImGui::Text("ESI %08X   EDI %08X", snap.esi, snap.edi);
    ImGui::Text("EBP %08X   ESP %08X", snap.ebp, snap.esp);
    ImGui::Text("EIP %08X   CS base %08X", snap.eip, snap.cs_base);
    ImGui::Separator();
    ImGui::Text("EFLAGS %08X", snap.eflags);
    static const struct { const char *name; uint32_t mask; } flags[] = {
        {"CF", F_CF}, {"PF", F_PF}, {"AF", F_AF}, {"ZF", F_ZF}, {"SF", F_SF},
        {"TF", F_TF}, {"IF", F_IF}, {"DF", F_DF}, {"OF", F_OF}};
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i)
    {
        const bool set = (snap.eflags & flags[i].mask) != 0;
        ImGui::TextColored(set ? ImVec4(0.4f, 1.f, 0.4f, 1.f) : ImVec4(0.5f, 0.5f, 0.5f, 1.f), "%s=%d", flags[i].name, set ? 1 : 0);
        if (i % 3 != 2 && i + 1 < sizeof(flags) / sizeof(flags[0]))
            ImGui::SameLine();
    }
    ImGui::Text("Next bytes:");
    ImGui::SameLine();
    for (int i = 0; i < 8; ++i)
    {
        ImGui::Text("%02X", snap.code[i]);
        if (i < 7)
            ImGui::SameLine();
    }
    ImGui::End();
}

// Hex dump of the guest's private RAM, 16 bytes per row with ASCII on the right.
static void draw_memory_window(X86Cpu &cpu)
{
    static int base = 0;
    static std::vector<uint8_t> row;
    const int rows = 16, cols = 16;

    ImGui::SetNextWindowSize(ImVec2(640, 380), ImGuiCond_FirstUseEver);
    ImGui::Begin("Memory");
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Address", &base, 16, 256, ImGuiInputTextFlags_CharsHexadecimal);
    base = std::max(0, std::min(base, 0x10000 - rows * cols)) & ~0xF;
    ImGui::SameLine();
    if (ImGui::Button("EIP"))
        base = static_cast<int>((cpu.get_eip() & 0xFFFF) & ~0xFu);
    ImGui::Separator();

    for (int r = 0; r < rows; ++r)
    {
        const uint32_t addr = base + r * cols;
        if (!cpu.read_local_ram(addr, cols, row))
            break;
        char line[128];
        int n = snprintf(line, sizeof(line), "%04X  ", addr);
        for (int c = 0; c < cols; ++c)
            n += snprintf(line + n, sizeof(line) - n, "%02X%s", row[c], c == 7 ? "  " : " ");
        n += snprintf(line + n, sizeof(line) - n, " ");
        for (int c = 0; c < cols; ++c)
            line[n++] = isprint(row[c]) ? static_cast<char>(row[c]) : '.';
        line[n] = 0;
        ImGui::TextUnformatted(line);
    }
    ImGui::End();
}

TextEditor editor;
TextEditor::ErrorMarkers markers;

namespace fs = std::filesystem;
static const fs::path boot_dir = fs::path(SPARKY_SOURCE_DIR) / "src" / "boot";

static std::string current_file; // file name within src/boot
static bool dirty = false;
static std::string pending_open; // file waiting for the "discard changes?" answer
static std::string status_msg;

static std::mutex build_mutex;
static std::string build_log;
static std::atomic<bool> build_running{false};
static std::atomic<bool> build_done{false};
static std::atomic<bool> build_ok{false};
static bool execute_after = false;
static bool execute_paused = false;
static bool show_build_log = false;
static std::thread build_thread;

static bool allowed_extension(const fs::path &p)
{
    const std::string ext = p.extension().string();
    return ext == ".asm" || ext == ".cpp" || ext == ".ld" || ext == ".h" || ext == ".c";
}

static std::vector<std::string> list_boot_files()
{
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(boot_dir, ec))
        if (entry.is_regular_file() && allowed_extension(entry.path()))
            files.push_back(entry.path().filename().string());
    std::sort(files.begin(), files.end());
    return files;
}

bool open_file(const std::string &name)
{
    std::ifstream in(boot_dir / name, std::ios::binary);
    if (!in)
    {
        status_msg = "Could not open " + name;
        return false;
    }
    editor.SetText(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
    const std::string ext = fs::path(name).extension().string();
    if (ext == ".cpp" || ext == ".h")
    {
        editor.SetColorizerEnable(true);
        editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
    }
    else if (ext == ".c")
    {
        editor.SetColorizerEnable(true);
        editor.SetLanguageDefinition(TextEditor::LanguageDefinition::C());
    }
    else
        editor.SetColorizerEnable(false); // no highlighter for asm or linker scripts
    current_file = name;
    dirty = false;
    status_msg.clear();
    return true;
}

static bool save_file()
{
    if (current_file.empty())
        return false;
    std::ofstream out(boot_dir / current_file, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        status_msg = "Could not write " + current_file;
        return false;
    }
    out << editor.GetText();
    out.flush();
    if (!out)
    {
        status_msg = "Error writing " + current_file;
        return false;
    }
    dirty = false;
    status_msg = "Saved " + current_file;
    return true;
}

static void append_log(const std::string &text)
{
    std::lock_guard<std::mutex> lock(build_mutex);
    build_log += text;
}

// Rebuilds the boot image on a worker thread so the UI stays responsive
static void start_build(bool then_execute, bool paused)
{
    if (build_running)
        return;
    if (dirty && !save_file())
    {
        show_build_log = true;
        append_log("Build cancelled: " + status_msg + "\n");
        return;
    }
    if (build_thread.joinable())
        build_thread.join();
    {
        std::lock_guard<std::mutex> lock(build_mutex);
        build_log.clear();
    }
    build_running = true;
    build_done = false;
    execute_after = then_execute;
    execute_paused = paused;
    show_build_log = true;
    build_thread = std::thread([] {
        const std::string cmd = std::string("cmake --build \"") + SPARKY_BUILD_DIR + "\" --target boot 2>&1";
        FILE *pipe = popen(cmd.c_str(), "r");
        if (!pipe)
        {
            append_log("Failed to run cmake\n");
            build_ok = false;
            build_done = true;
            return;
        }
        char buf[512];
        while (fgets(buf, sizeof(buf), pipe))
            append_log(buf);
        const int rc = pclose(pipe);
        build_ok = (rc == 0);
        append_log(rc == 0 ? "\n--- Build succeeded ---\n" : "\n--- Build FAILED ---\n");
        build_done = true;
    });
}

// Runs on the UI thread once per frame
static void poll_build()
{
    if (!build_done.exchange(false))
        return;
    if (build_thread.joinable())
        build_thread.join();
    build_running = false;
    if (build_ok && execute_after)
    {
        if (restart_guest(execute_paused))
            append_log("--- Guest restarted ---\n");
        else
            append_log("--- Could not load pmode_core.bin ---\n");
    }
    execute_after = false;
}

void simgui_shutdown()
{
    if (build_thread.joinable())
        build_thread.join();
}

static void draw_build_window()
{
    if (!show_build_log)
        return;
    ImGui::SetNextWindowSize(ImVec2(640, 240), ImGuiCond_FirstUseEver);
    ImGui::Begin("Build output", &show_build_log);
    std::string log;
    {
        std::lock_guard<std::mutex> lock(build_mutex);
        log = build_log;
    }
    ImGui::BeginChild("log", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(log.c_str());
    if (build_running)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

void update_editor(X86Cpu &cpu)
{


        sl::gui_new_frame();
        poll_build();
        auto cpos = editor.GetCursorPosition();
        ImGui::Begin("Code", nullptr, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_MenuBar);
        ImGui::SetWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::BeginMenu("Open"))
                {
                    for (const auto &name : list_boot_files())
                        if (ImGui::MenuItem(name.c_str(), nullptr, name == current_file))
                        {
                            if (dirty)
                                pending_open = name;
                            else
                                open_file(name);
                        }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Save", "Ctrl-S", nullptr, !current_file.empty() && dirty))
                    save_file();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Build"))
            {
                const bool idle = !build_running;
                if (ImGui::MenuItem("Make", nullptr, nullptr, idle))
                    start_build(false, false);
                if (ImGui::MenuItem("Execute", nullptr, nullptr, idle))
                    start_build(true, false);
                if (ImGui::MenuItem("Execute paused (step)", nullptr, nullptr, idle))
                    start_build(true, true);
                ImGui::Separator();
                ImGui::MenuItem("Show build output", nullptr, &show_build_log);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                bool ro = editor.IsReadOnly();
                if (ImGui::MenuItem("Read-only mode", nullptr, &ro))
                    editor.SetReadOnly(ro);
                ImGui::Separator();

                if (ImGui::MenuItem("Undo", "ALT-Backspace", nullptr, !ro && editor.CanUndo()))
                    editor.Undo();
                if (ImGui::MenuItem("Redo", "Ctrl-Y", nullptr, !ro && editor.CanRedo()))
                    editor.Redo();

                ImGui::Separator();

                if (ImGui::MenuItem("Copy", "Ctrl-C", nullptr, editor.HasSelection()))
                    editor.Copy();
                if (ImGui::MenuItem("Cut", "Ctrl-X", nullptr, !ro && editor.HasSelection()))
                    editor.Cut();
                if (ImGui::MenuItem("Delete", "Del", nullptr, !ro && editor.HasSelection()))
                    editor.Delete();
                if (ImGui::MenuItem("Paste", "Ctrl-V", nullptr, !ro && ImGui::GetClipboardText() != nullptr))
                    editor.Paste();

                ImGui::Separator();

                if (ImGui::MenuItem("Select all", nullptr, nullptr))
                    editor.SetSelection(TextEditor::Coordinates(), TextEditor::Coordinates(editor.GetTotalLines(), 0));

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                if (ImGui::MenuItem("Dark palette"))
                    editor.SetPalette(TextEditor::GetDarkPalette());
                if (ImGui::MenuItem("Light palette"))
                    editor.SetPalette(TextEditor::GetLightPalette());
                if (ImGui::MenuItem("Retro blue palette"))
                    editor.SetPalette(TextEditor::GetRetroBluePalette());
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if (!pending_open.empty())
        {
            ImGui::OpenPopup("Unsaved changes");
        }
        if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Discard unsaved changes to %s?", current_file.c_str());
            if (ImGui::Button("Save and open") && save_file())
            {
                open_file(pending_open);
                pending_open.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard"))
            {
                open_file(pending_open);
                pending_open.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                pending_open.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (!current_file.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
            save_file();

        ImGui::Text("%6d/%-6d %6d lines  | %s | %s | %s | %s", cpos.mLine + 1, cpos.mColumn + 1, editor.GetTotalLines(),
                    editor.IsOverwrite() ? "Ovr" : "Ins", editor.CanUndo() ? "*" : " ",
                    current_file.c_str(), dirty ? "modified" : "saved");
        if (!status_msg.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", status_msg.c_str());
        }

        editor.Render("TextEditor");
        if (editor.IsTextChanged())
            dirty = true;
        ImGui::End();

        draw_build_window();
        draw_registers_window(cpu);
        draw_memory_window(cpu);

        sl::gui_render();

}