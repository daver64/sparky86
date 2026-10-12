#include "TextEditor.h"
#include "keymap.h"
#include "system_bus.h"
#include "x86cpu.h"
#include <cctype>
#include <cstdio>

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
const char *fileToEdit = "src/boot/boot.asm";
void update_editor(X86Cpu &cpu)
{


        sl::gui_new_frame();
        auto cpos = editor.GetCursorPosition();
        ImGui::Begin("Code", nullptr, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_MenuBar);
        ImGui::SetWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save"))
                {
                    auto textToSave = editor.GetText();
                    /// save text....
                }
                //if (ImGui::MenuItem("Quit", "Alt-F4"))
                //    break;
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

        ImGui::Text("%6d/%-6d %6d lines  | %s | %s | %s | %s", cpos.mLine + 1, cpos.mColumn + 1, editor.GetTotalLines(),
                    editor.IsOverwrite() ? "Ovr" : "Ins", editor.CanUndo() ? "*" : " ",
                    editor.GetLanguageDefinition().mName.c_str(), fileToEdit);

        editor.Render("TextEditor");
        ImGui::End();

        draw_registers_window(cpu);
        draw_memory_window(cpu);

        sl::gui_render();

}