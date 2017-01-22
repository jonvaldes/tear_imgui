
#include "../imgui.h"
#include <stdio.h>
#include "../termbox/src/termbox.h"

bool   is_this_on          = false;
bool   is_this_also_on     = false;
bool   show_test_window    = false;
bool   show_another_window = false;
ImColor clear_color         = ImColor(114, 144);

void setupKeyHandling() {
    ImGuiIO &io                   = ImGui::GetIO();
    io.KeyMap[ImGuiKey_Backspace] = ImGuiKey_Backspace;
    io.KeyMap[ImGuiKey_Enter]     = ImGuiKey_Enter;
    return;
    io.KeyMap[ImGuiKey_Tab]       = TB_KEY_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = TB_KEY_ARROW_LEFT;
    return;
    io.KeyMap[ImGuiKey_RightArrow] = TB_KEY_ARROW_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow]    = TB_KEY_ARROW_UP;
    io.KeyMap[ImGuiKey_DownArrow]  = TB_KEY_ARROW_DOWN;
    io.KeyMap[ImGuiKey_PageUp]     = TB_KEY_PGUP;
    io.KeyMap[ImGuiKey_PageDown]   = TB_KEY_PGDN;
    io.KeyMap[ImGuiKey_Home]       = TB_KEY_HOME;
    io.KeyMap[ImGuiKey_End]        = TB_KEY_END;
    io.KeyMap[ImGuiKey_Delete]     = TB_KEY_DELETE;
    io.KeyMap[ImGuiKey_Backspace]  = TB_KEY_BACKSPACE2;
    io.KeyMap[ImGuiKey_Enter]      = TB_KEY_ENTER;
    io.KeyMap[ImGuiKey_Escape]     = TB_KEY_ESC;
    /*
    io.KeyMap[ImGuiKey_A] = SDLK_a;
    io.KeyMap[ImGuiKey_C] = SDLK_c;
    io.KeyMap[ImGuiKey_V] = SDLK_v;
    io.KeyMap[ImGuiKey_X] = SDLK_x;
    io.KeyMap[ImGuiKey_Y] = SDLK_y;
    io.KeyMap[ImGuiKey_Z] = SDLK_z;
	*/
}

void render() {
    ImGuiIO &io = ImGui::GetIO();

    // Setup display size (every frame to accommodate for window resizing)
    int w                      = tb_width();
    int h                      = tb_height();
    io.DisplaySize             = ImVec2((float)w, (float)h);
    io.DisplayFramebufferScale = ImVec2(1, 1);

    // ------------------------------
	tb_set_clear_attributes(0,ImGui::GetImColor(ImGuiCol_WindowBg).bg);
    tb_clear();
    ImGui::NewFrame();
    ImGui::LogToFile(0, "logfile");

    // 1. Show a simple window
    // Tip: if we don't call ImGui::Begin()/ImGui::End() the widgets appears in a window automatically called "Debug"
    {
        ImGui::SetWindowSize(ImVec2(45, 30), ImGuiSetCond_Always);
        static float f = 0.0f;
        ImGui::Text("Hello, world!");
        ImGui::Text("Hello, world!");
        ImGui::Spacing();
        ImGui::Text("Hello, world!");
        if(ImGui::Button("Test Window"))
            show_test_window ^= 1;

        ImGui::Checkbox("backspace down", &io.KeysDown[ImGuiKey_Backspace]);
        ImGui::Checkbox("Is this on", &is_this_on);
        ImGui::Checkbox("Is this also on", &is_this_also_on);
        ImGui::Separator();

        ImGui::Text("Hello, world!2");
        ImGui::InputFloat("Value", &f);
        ImGui::ProgressBar(f);
        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
        if(ImGui::Button("Another Window"))
            show_another_window ^= 1;
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    }

    // 2. Show another simple window, this time using an explicit Begin/End pair
    if(show_another_window) {
        ImGui::SetNextWindowPos(ImVec2(2, 4), ImGuiSetCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(20, 10), ImGuiSetCond_FirstUseEver);
        ImGui::Begin("Another Window", &show_another_window);
        ImGui::End();
    }

    if(show_test_window) {
        ImGui::SetNextWindowPos(ImVec2(2, 5), ImGuiSetCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(80, 30), ImGuiSetCond_FirstUseEver);
        ImGui::Begin("Style Editor", 0);
        ImGui::ShowTestWindow(0);
        ImGui::ShowStyleEditor();
        ImGui::End();
    }

    // Rendering
    ImGui::Render();
    tb_present();
}

int main() {

    tb_init();
	tb_select_output_mode(TB_OUTPUT_256);
    tb_select_input_mode(TB_INPUT_ESC | TB_INPUT_MOUSE);
    setupKeyHandling();

    // Main loop
    bool done = false;
    while(!done) {

        ImGuiIO &io = ImGui::GetIO();
        // termbox doesn't give us keyup events, so do as if all keys are only pressed for one frame
        for(int i = 0; i < ImGuiKey_COUNT; i++) {
            if(io.KeysDown[i]) {
                io.KeysDown[i] = false;
            }
        }

        // Setup inputs
        struct tb_event ev;

        if(tb_peek_event(&ev, 16) > 0) {
            switch(ev.type) {
            case TB_EVENT_MOUSE: {
                switch(ev.key) {
                case TB_KEY_MOUSE_WHEEL_UP: {
                    io.MouseWheel = 1;
                } break;
                case TB_KEY_MOUSE_WHEEL_DOWN: {
                    io.MouseWheel = -1;
                } break;
                case TB_KEY_MOUSE_RELEASE: {
                    io.MouseDown[0] = false;
                    io.MouseDown[1] = false;

                } break;
                default:
                    io.MousePos     = ImVec2((float)ev.x, (float)ev.y);
                    io.MouseDown[0] = true;
                }
            } break;
            case TB_EVENT_KEY: {
                if(ev.key == TB_KEY_ESC) {
                    done = true;
                    break;
                }
                io.AddInputCharacter(ev.ch);
                if(ev.key == TB_KEY_BACKSPACE2) {
                    io.KeysDown[ImGuiKey_Backspace] = true;
                }
                if(ev.key == TB_KEY_ENTER) {
                    io.KeysDown[ImGuiKey_Enter] = true;
                }
            } break;
            }
        }
        render();
    }

    // Cleanup
    ImGui::Shutdown();
    tb_shutdown();

    return 0;
}
